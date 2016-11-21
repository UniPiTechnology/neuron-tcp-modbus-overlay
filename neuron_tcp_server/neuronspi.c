/*
 * SPI communication with UniPi Neuron family controllers
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
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <string.h>
#include <sys/mman.h>

#include "armspi.h"


//#define FILEMODE S_IRWXU | S_IRGRP | S_IROTH

int main(int argc, char *argv[])
{
    const char* fwdir = "/opt/fw/";

    arm_handle* arm = malloc(sizeof(arm_handle));

    arm_init(arm, "/dev/spidev0.1", 12000000, 0, NULL);

    idle_op(arm);

    uint16_t buffer[128];
    buffer[0] = 10;
    buffer[1] = 1;
    //write_regs(arm, 7, 2, buffer);

    int i, n;
    //for (i=0; i<10000; i++) {
        n = read_regs(arm, 0, 10, buffer);
    //}

    printf("cnt reg=%d\n",n);
    for (i=0; i<10; i++) {
       printf("%5d,",buffer[i]);
    }
    printf("\n");

    read_regs(arm, 1000, 7, buffer);
    for (i=0; i<7; i++) {
       printf("%5d,",buffer[i]);
    }
    printf("\n");

    int n2000 = read_regs(arm, 2000, 30, buffer);
    printf("n2000=%d\n", n2000);
    for (i=0; i < n2000; i++) {
       printf("%5d,",buffer[i]);
    }
    printf("\n");

    
    /* Firmware programming */
    /*
    int fd, ret;
    const char* armname = arm_name(arm);
    char* fwname = malloc(strlen(fwdir) + strlen(armname) + strlen(".bin") + 1);
    strcpy(fwname, fwdir);
    strcat(fwname, armname);
    strcat(fwname, ".bin");
    
    if((fd = open(fwname, O_RDONLY)) >= 0) {
        struct stat st;
        if((ret=fstat(fd,&st)) >= 0) {
            size_t len_file = st.st_size;
            printf("Sending firware file %s length=%d\n", fwname, len_file);
            void * data;
            if((data=mmap(NULL, len_file, PROT_READ,MAP_PRIVATE, fd, 0)) != MAP_FAILED) {
                send_firmware(arm, (uint8_t*) data, len_file, 0);
                munmap(data, len_file);
            } else {
                printf("Error mapping firmware file %s to memory\n", fwname);
            }
        }
        close(fd);
    } else {
        printf("Error opening firmware file %s\n", fwname);
    }
    free(fwname);
    */
    /*
    //for (i=0; i<10000; i++) {
    //    if (read_regs(arm, 0, 20, buffer) <= 0 ) { printf("ERR reg 0 iter=%d\n",i); }
          //if (read_regs(arm, 0, 50, buffer) <= 0 ) { printf("ERR reg 0 iter=%d\n",i); }
    //    if (read_regs(arm, 1000, 35, buffer) <= 0 ) { printf("ERR reg 1000 iter=%d\n",i); }
    //} 
    
    
    buffer[0] = 0x10;
    for (i=0; i<10000; i++) {
        //buffer[0] ^= 0x0f;
        buffer[0] >>= 1;
        write_regs(arm, 20, 1, buffer);
        if (buffer[0] == 0) buffer[0] = 0x10;
        usleep(50000);
    }
    
    buffer[0]=0x3;
    //write_bits(arm, 1, 2, (uint8_t*)buffer);
    // n = write_bit(arm, 1002, 1);
    //printf("b1 =%d\n",n);
    //n = write_bit(arm, 10, 1);
    //printf("b10 =%d\n",n);

    //n = read_bits(arm, 0, 16, (uint8_t*)buffer);
    //printf("cnt =%d  %5x \n",n, buffer[0]);
    */

    close(arm->fd);
    return 0;
}

