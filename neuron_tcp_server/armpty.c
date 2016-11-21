/*
 * UART simulator for UniPi Neuron family controllers using pty
 *
 * Copyright (c) 2016  Faster CZ, ondra@faster.cz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 */

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pty.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <termios.h>

#include "armspi.h"

/* missing c_lflag.
*    Required to set this termios together with packet mode
*    Provides signaling master after ioctl settings of slave pty
*/

//#define IEXTPROC  0x10000  // pozor; v termbits.h je hodnota oktalove
#define IEXTPROC  EXTPROC


int armpty_open(arm_handle* arm, uint8_t uart)
{
    static extcomm_counter = 0;
    int masterfd;
    int slavefd;
    uint8_t circuit = arm->index;
    char slavename[256];
    const char dirname[] = "/dev/extcomm";
    char tmp[265];

    arm->uart_q[uart].masterpty = -1;
    int n = openpty(&masterfd, &slavefd, slavename, NULL, NULL);
    if (n < 0) {
        if (arm_verbose) printf("Error opening pty\n");
        return n;
    }
    if (arm_verbose) printf("Board%d PTY%d %s\n", arm->index, uart, slavename);

    if ((mkdir(dirname, 0750) < 0) && (errno != EEXIST)) {
        perror("Error creating dir /dev/extcomm");
        close(masterfd);
        return -1; 
    }
    if (extcomm_counter == 0) {
        system("rm -rf /dev/extcomm/0");
        sprintf(tmp, "%s/0", dirname);
        if ((mkdir(tmp, 0750) < 0)&& (errno != EEXIST)) {
            perror("Error creating dir /dev/extcomm/0");
            close(masterfd);
            return -1;
        }
    }
    sprintf(tmp, "%s/0/%d", dirname, extcomm_counter);
    n = symlink(slavename, tmp);
    if (n < 0) {
        perror("Error creating symlink /dev/extcomm/x/x");
        close(masterfd);
        return -1;
    }
    extcomm_counter++;

    sprintf(tmp, "%s/%d", dirname, circuit+1);
    if ((mkdir(tmp, 0750) < 0)&& (errno != EEXIST)) {
        perror("Error creating dir /dev/extcomm/x");
        close(masterfd);
        return -1;
    }
    sprintf(tmp, "%s/%d/%d", dirname, circuit+1, uart);
    unlink(tmp);
    n = symlink(slavename, tmp);
    if (n < 0) {
        perror("Error creating symlink /dev/extcomm/x/x");
        close(masterfd);
        return -1;
    }
    // set packet mode
    //int dta[2] = {1,0};
    int dta = 1;
    n = ioctl(masterfd, TIOCPKT, &dta);
    if (n < 0) {
        perror("Error termios ");
        close(masterfd);
        return -1;
    }
    struct termios tc;
    n = tcgetattr(masterfd, &tc);
    if (n < 0) {
        perror("Error tcgetattr ");
        close(masterfd);
        return -1;
    }
    //raw mode for data processing 
    cfmakeraw(&tc);
    tc.c_lflag |= IEXTPROC;

    n = tcsetattr(masterfd, TCSANOW, &tc);
    if (n < 0) {
        perror("Error tcsetattr ");
        close(masterfd);
        return -1;
    }

    int flags = fcntl(masterfd, F_GETFL);
    if (flags == -1) {
        perror("Error fcntl_get ");
        close(masterfd);
        return -1;
    }
    n = fcntl(masterfd, F_SETFL, flags | O_NONBLOCK);
    if (n == -1) {
        perror("Error fcntl_set  NONBLOCK");
        close(masterfd);
        return -1;
    }
    arm->uart_q[uart].masterpty = masterfd;
    uint16_t interrupt_mask = 5;
    if (arm->sw_version)
        write_regs(arm, arm->int_mask_register, 1, &interrupt_mask);
    return masterfd;
}

void dpr(uint8_t* buffer, int len, const char* msg)
{
    int i;
    printf(msg);
    for (i = 0; i < len; i++) {
        printf(" %02x", buffer[i]);
    }

    printf("\n");
}



int armpty_setuart(int masterfd, arm_handle* arm, uint8_t uart)
{
    struct termios tc;
    char tmp[16];
    uint16_t conf[2];
    int n;

    n = tcgetattr(masterfd, &tc);
    if (n < 0) {
        perror("Error tcgetattr ");
        return -1;
    }    
    tc.c_lflag |= IEXTPROC;

    conf[0] = tc.c_cflag & CBAUD;
    if (conf[0] != 0) {
        conf[1] = 0x0000; // RTU ?
        //n = tcsetattr(masterfd, TCSANOW, &tc);
        //if (n < 0) {
        //    perror("Error tcsetattr ");
        //    return -1;
        //}
        //write_regs(arm, 1018, 1, conf);
            write_regs(arm, 100+2*uart, 1 /*2*/, conf);
        //}
        //write_regs(arm, 101+2*uart, 1, &rtu);
    }
    int rd = read(masterfd, tmp, sizeof(tmp));  // must read sone chars from pty
    //printf("CF: %04x %x %d %x %x", conf[0], tc.c_cflag, tc.c_lflag, tc.c_iflag, tc.c_oflag);
    //dpr( tmp, rd, "");
}


int armpty_readpty(int masterfd, arm_handle* arm, uint8_t uart)
{
    uint8_t buffer[256+1];
    // ToDo while ... EAGAIN
    int rd = read(masterfd, buffer, sizeof(buffer));
    //if (rd >1)
    //    dpr(buffer+1, rd-1, "WR: ");

    if (rd > 2) {
        write_string(arm, uart, buffer + 1, rd - 1);
    } else if (rd > 1) {
        write_char(arm, uart, buffer[1]);
    }
}

int armpty_readuart(arm_handle* arm, int do_idle)
{
    /* ... ToDo more uarts ...  */
    uint8_t uart = 0;
    uint8_t buffer[256];
    int n;
    int wanted = sizeof(buffer);
    int nr = 0;

    if (do_idle) idle_op(arm);
    while ((n = arm->uart_q[uart].remain) > 0) {
        if (n > wanted) n = wanted;
        n = read_string(arm, uart, buffer + nr, n);
        if (n < 0) break;
        wanted -= n; 
        nr += n;
        if (wanted == 0) {
            if ((arm->uart_q[uart].masterpty != -1)) {
                //dpr(buffer, nr, "RD: ");
                int nw = write(arm->uart_q[uart].masterpty, buffer, nr);
                if (nr != nw ) 
                    { if (arm_verbose) printf("1 wr: nr=%d nw=%d\n", nr,nw); }
            }
            nr = 0;
            wanted = sizeof(buffer);
        }
    }
    n = read_qstring(arm, uart, buffer + nr, wanted);
    nr += n;
    if (nr > 0) {
        if ((arm->uart_q[uart].masterpty != -1)) {
            //dpr(buffer, nr, "RD: ");
            int nw = write(arm->uart_q[uart].masterpty, buffer, nr);
            if (nr != nw )
                { if (arm_verbose) printf("2 wr: nr=%d nw=%d\n", nr,nw); }
        }
        nr = 0;
    }
    if (n == wanted) {
        n = read_qstring(arm, uart, buffer, sizeof(buffer));
        if (n > 0) {
            //dpr(buffer, nr, "RD: ");
            int nw = write(arm->uart_q[uart].masterpty, buffer, nr);
            if (nr != nw )
                { if (arm_verbose) printf("3 wr: nr=%d nw=%d\n", nr,nw); }
        }
    }
}
