/*
 * SPI communication with UniPi Neuron family controllers
 *
 * Copyright (c) 2016  Faster CZ, ondra@faster.cz
 * Copyright (c) 2007  MontaVista Software, Inc.
 * Copyright (c) 2007  Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * Cross-compile with cross-gcc -I/path/to/cross-kernel/include
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
#include "armspi.h"
#include "armutil.h"

// brain/modbus_prot.h
#define ARM_OP_READ_BIT   1
#define ARM_OP_READ_REG   4
#define ARM_OP_WRITE_BIT  5
#define ARM_OP_WRITE_REG  6
#define ARM_OP_WRITE_BITS 15

#define ARM_OP_WRITE_CHAR  65
#define ARM_OP_WRITE_STR   100
#define ARM_OP_READ_STR    101

#define ARM_OP_IDLE        0xfa


// !!!! on RPI 2,3 doesn't work transfer longer then 94 bytes. Must be divided into chunks
//#define _MAX_SPI_RX  94
#define _MAX_SPI_RX  64
//#define _MAX_SPI_RX  256


#define ac_header(buf) ((arm_comm_header*)buf)
#define ach_header(buf) ((arm_comm_chr_header*)buf)
#define acs_header(buf) ((arm_comm_str_header*)buf)

#define IDLE_PATTERN 0x0e5500fa
// hodnota 240 znaku by pravdepodobne mela byt spise 
//    255 - sizeof(arm_comm_header) = 251 -> 250(sude cislo) znaku 
// vyzkouset jestli neni problem v firmware
#define SPI_STR_MAX 240
#define NSS_PAUSE_DEFAULT  10

//static int be_quiet = 0;
int arm_verbose = 0;
int nss_pause = NSS_PAUSE_DEFAULT;
static void pabort(const char *s)
{
    if (arm_verbose > 0) perror(s);
    //abort();
}


uint8_t get_spi_mode(int fd)
{
    uint8_t mode;

    int ret = ioctl(fd, SPI_IOC_RD_MODE, &mode);
    if (ret >= 0) {
        return mode;
    }
    return ret;
}

uint32_t get_spi_speed(int fd)
{
    uint32_t speed;
    int ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
    if (ret >= 0) {
        return speed;
    }
    return ret;
    //SPI_IOC_RD_BITS_PER_WORD,
    //SPI_IOC_RD_LSB_FIRST,
    //SPI_IOC_WR_MODE32,
}

void set_spi_mode(int fd, uint8_t mode)
{
    int ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
    if (ret < 0) {
        pabort("Cannot set mode");
    }
}

void set_spi_speed(int fd, uint32_t speed)
{
    int ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    if (ret < 0) {
        pabort("Cannot set speed" );
    }
}

void queue_uart(uart_queue* queue, uint8_t chr1, uint8_t len)
{
    queue->remain = (len==0) ? 255 : len - 1;  // len==0 means 256 byte in remote queue
    if (queue->index < MAX_LOCAL_QUEUE_LEN) {
        queue->buffer[queue->index++] = chr1;
    } else {
        queue->overflow++;
    }
}

int one_phase_op(arm_handle* arm, uint8_t op, uint16_t reg, uint8_t value)
{
    int ret;
    arm->tx1.op = op;
    arm->tx1.len = value;
    arm->tx1.reg = reg;
    arm->tx1.crc = SpiCrcString((uint8_t*)&arm->tx1, SIZEOF_HEADER, 0);

    arm->tr[1].delay_usecs = 0;
    ret = ioctl(arm->fd, SPI_IOC_MESSAGE(2), arm->tr);
    if (ret < 1) {
        pabort("Can't send one-phase spi message");
        return -1;
    }
    uint16_t crc = SpiCrcString((uint8_t*)&arm->rx1, SIZEOF_HEADER,0);
    if (crc != arm->rx1.crc) {
        pabort("Bad crc in one-phase operation");
        return -1;
    }

    if ((*((uint32_t*)&arm->rx1) & 0xffff00ff) == IDLE_PATTERN) { 
        return 0;
    }
    if (arm->rx1.op == ARM_OP_WRITE_CHAR) { 
        // we received character from UART
        // doplnit adresaci uartu &arm->uart_q[0..4]
        queue_uart(arm->uart_q, ach_header(&arm->rx1)->ch1, ach_header(&arm->rx1)->len);
        return 0;
    }
    pabort("Unexpcted reply in one-phase operation");
    return -1;
}

char errmsg[256];
int two_phase_op(arm_handle* arm, uint8_t op, uint16_t reg, uint16_t len2)
{
    int ret;
    uint16_t tr_len2;
    uint16_t crc;
    // Prepare chunk1
    arm->tx1.op = op;
    arm->tx1.reg = reg;
    arm->tx1.len = len2 & 0xff;        //set len in chunk1 to length of chunk2 (without crc)
    arm->tr[1].delay_usecs = 25;              // set delay after first phase
    if (op != ARM_OP_WRITE_STR) {
        ac_header(arm->tx2)->op  = op;  // op and reg in chunk2 is the same
        ac_header(arm->tx2)->reg = reg;
        if (len2 > 60) {
            arm->tr[1].delay_usecs += (len2-60)/2;  // add more delay
        }
    }
    tr_len2 = (len2 & 1) ? len2+1 : len2;         //transaction length must be even
    crc = SpiCrcString((uint8_t*)&arm->tx1, SIZEOF_HEADER, 0);
    arm->tx1.crc = crc;                           // crc of first phase
    crc = SpiCrcString(arm->tx2, tr_len2, crc);   // crc of second phase
    ((uint16_t*)arm->tx2)[tr_len2>>1] = crc;

    ac_header(arm->rx2)->op  = op;                // 'destroy' content of receiving buffer
    uint32_t total = tr_len2 + CRC_SIZE;

    if (total <= _MAX_SPI_RX) {
        arm->tr[2].len = total;
        ret = ioctl(arm->fd, SPI_IOC_MESSAGE(3), arm->tr);
    } else if (total <= (2*_MAX_SPI_RX)) {
        arm->tr[2].len = _MAX_SPI_RX;
        arm->tr[3].len = total - _MAX_SPI_RX;
        ret = ioctl(arm->fd, SPI_IOC_MESSAGE(4), arm->tr);
    } else if (total <= (3*_MAX_SPI_RX)) {
        arm->tr[2].len = _MAX_SPI_RX;
        arm->tr[3].len = _MAX_SPI_RX;
        arm->tr[4].len = total - (2*_MAX_SPI_RX);
        ret = ioctl(arm->fd, SPI_IOC_MESSAGE(5), arm->tr);
    } else if (total <= (4*_MAX_SPI_RX)) {
        arm->tr[2].len = _MAX_SPI_RX;
        arm->tr[3].len = _MAX_SPI_RX;
        arm->tr[4].len = _MAX_SPI_RX;
        arm->tr[5].len = total - (3*_MAX_SPI_RX);
        ret = ioctl(arm->fd, SPI_IOC_MESSAGE(6), arm->tr);
    } else {
        arm->tr[2].len = _MAX_SPI_RX;
        arm->tr[3].len = _MAX_SPI_RX;
        arm->tr[4].len = _MAX_SPI_RX;
        arm->tr[5].len = _MAX_SPI_RX;
        arm->tr[6].len = total - (4*_MAX_SPI_RX);
        ret = ioctl(arm->fd, SPI_IOC_MESSAGE(7), arm->tr);
    }

    //printf("ret2=%d\n", ret);
    if (ret < 1) {
        pabort("can't send two-phase spi message");
        return ret;
    }

    //return -1; // ---------------- smazat 
    
    //printf("rx1=%x\n", *((uint32_t*)&arm->rx1));
    crc = SpiCrcString((uint8_t*)&arm->rx1, SIZEOF_HEADER, 0);
    if (crc != arm->rx1.crc) {
        pabort("Bad 1.crc in two phase operation");
        return -1;
    }

    crc = SpiCrcString(arm->rx2, tr_len2, crc);

    if (arm->rx1.op == ARM_OP_WRITE_CHAR) { 
        // we received character from UART
        // doplnit adresaci uartu!
        queue_uart(arm->uart_q, ach_header(&arm->rx1)->ch1, ach_header(&arm->rx1)->len);
        if (((uint16_t*)arm->rx2)[tr_len2>>1] != crc) {
            pabort("Bad 2.crc in two phase operation");
            return -1;
        }
        return 0;
    }
    if (((uint16_t*)arm->rx2)[tr_len2>>1] != crc) {
        pabort("Bad 2.crc in two phase operation");
        return -1;
    }
    if ((*((uint32_t*)&arm->rx1) & 0xffff00ff) == IDLE_PATTERN) {
        return 0;
    }
    sprintf(errmsg,"Unexpcted reply in two phase operation %02x %02x %04x %04x", 
            arm->rx1.op, arm->rx1.len, arm->rx1.reg, arm->rx1.crc);
    pabort(errmsg);
    return -1;
}

int idle_op(arm_handle* arm)
{
    int backup = arm_verbose;
    arm_verbose = 0;
    int n = one_phase_op(arm, ARM_OP_IDLE, 0x0e55, 0);
    arm_verbose = backup;;
    return n;
}

int read_regs(arm_handle* arm, uint16_t reg, uint8_t cnt, uint16_t* result)
{
    uint16_t len2 = SIZEOF_HEADER + sizeof(uint16_t) * cnt;
    int ret = two_phase_op(arm, ARM_OP_READ_REG, reg, len2);
    if (ret < 0) {
        return ret;
    }

    if ((ac_header(arm->rx2)->op != ARM_OP_READ_REG) || 
        (ac_header(arm->rx2)->len > cnt) ||
        (ac_header(arm->rx2)->reg != reg)) {
            pabort("Unexpected reply in READ_REG");
            return -1;
    }
    cnt =  ac_header(arm->rx2)->len;
    memmove(result, arm->rx2+SIZEOF_HEADER, cnt * sizeof(uint16_t));
    return cnt;
}

int write_regs(arm_handle* arm, uint16_t reg, uint8_t cnt, uint16_t* values)
{
    if (cnt > 126) {
        pabort("Too many registers in WRITE_REG");
        return -1;
    }

    uint16_t len2 = SIZEOF_HEADER + sizeof(uint16_t) * cnt;

    ac_header(arm->tx2)->len = cnt;
    memmove(arm->tx2 + SIZEOF_HEADER, values, cnt * sizeof(uint16_t));

    int ret = two_phase_op(arm, ARM_OP_WRITE_REG, reg, len2);
    if (ret < 0) {
        return ret;
    }

    if (ac_header(arm->rx2)->op != ARM_OP_WRITE_REG) {
        pabort("Unexpcted reply in WRITE_REG");
        return -1;
    }
    cnt =  ac_header(arm->rx2)->len;
    return cnt;
}

int read_bits(arm_handle* arm, uint16_t reg, uint16_t cnt, uint8_t* result)
{
    uint16_t len2 = SIZEOF_HEADER + (((cnt+15) >> 4) << 1);  // trunc to 16bit in bytes
    if (len2 > 256){
        pabort("Too many registers in READ_BITS");
        return -1;
    }
    int ret = two_phase_op(arm, ARM_OP_READ_BIT, reg, len2);
    if (ret < 0) {
        return ret;
    }

    if ((ac_header(arm->rx2)->op != ARM_OP_READ_BIT) || 
        (ac_header(arm->rx2)->reg != reg)) {
            pabort("Unexpcted reply in READ_BIT");
            return -1;
    }
    cnt = ac_header(arm->rx2)->len;
    memmove(result, arm->rx2+SIZEOF_HEADER, ((cnt+7) >> 3));    // trunc to 8 bit
    return cnt;
}

int write_bit(arm_handle* arm, uint16_t reg, uint8_t value)
{
    int ret = one_phase_op(arm, ARM_OP_WRITE_BIT, reg, !(!value));
    if (ret < 0) {
        return ret;
    }
    return 1;
}

int write_bits(arm_handle* arm, uint16_t reg, uint16_t cnt, uint8_t* values)
{
    uint16_t len2 = SIZEOF_HEADER + (((cnt+15) >> 4) << 1);  // trunc to 16bit in bytes
    if (len2 > 256) {
        pabort("Too many registers in WRITE_BITS");
        return -1;
    }

    ac_header(arm->tx2)->len = cnt;
    memmove(arm->tx2 + SIZEOF_HEADER, values, ((cnt+7) >> 3));

    int ret = two_phase_op(arm, ARM_OP_WRITE_BITS, reg, len2);
    if (ret < 0) {
        return ret;
    }

    if (ac_header(arm->rx2)->op != ARM_OP_WRITE_BITS) {
        pabort("Unexpcted reply in WRITE_REG");
        return -1;
    }
    if (cnt > ac_header(arm->rx2)->len)
       cnt = ac_header(arm->rx2)->len;
    return cnt;
}


int write_char(arm_handle* arm, uint8_t uart, uint8_t c)
{
    int ret = one_phase_op(arm, ARM_OP_WRITE_CHAR, uart, c);
    if (ret < 0) {
        return ret;
    }
    return 1;
}

int write_string(arm_handle* arm, uint8_t uart, uint8_t* str, int len)
{

    if ((len > 256) || (len<=0)) {
        pabort("Bad string length(1..256)");
        return -1;
    }
    uint16_t len2 = len;

    memmove(arm->tx2, str, len2);

    int ret = two_phase_op(arm, ARM_OP_WRITE_STR, uart, len2);

    if (ac_header(arm->rx2)->op != ARM_OP_WRITE_STR) {
        pabort("Unexpcted reply in WRITE_STR");
        return -1;
    }
    return ac_header(arm->rx2)->len;
    //return cnt;
}

int read_string(arm_handle* arm, uint8_t uart, uint8_t* str, int cnt)
{
    if (uart > 0) {
        pabort("Bad parameter uart");
        return -1;
    }
    uart_queue* queue = &arm->uart_q[uart];
    uint16_t len2 = cnt;

    if (len2 % 2) len2++;
    if (len2 > SPI_STR_MAX) len2 = SPI_STR_MAX;
    len2 = len2 + SIZEOF_HEADER;

    int ret =  two_phase_op(arm, ARM_OP_READ_STR, uart, len2);
    if (ret < 0) {
        if (arm_verbose) printf("Error read str %d %d\n", cnt, len2);
        return ret;
    }

    if (ac_header(arm->rx2)->op != ARM_OP_READ_STR) {
        //if (arm_rx2_str.len > cnt):
        pabort("Unexpcted reply in READ_STR");
        return -1;
    }
    uint16_t rcnt = acs_header(arm->rx2)->len;    // length of received string
    queue->remain = acs_header(arm->rx2)->remain; // remains in remote queue
    // join uart_queue and rcnt chars
    int n = queue->index < cnt ? queue->index : cnt;
    if (n > 0) {
        memmove(str, queue->buffer, n);
        if (n < queue->index) {  // str is too short for import local queue
            memmove(queue->buffer, queue->buffer + n, queue->index - n);
            queue->index -= n;
            if (queue->index + rcnt > MAX_LOCAL_QUEUE_LEN) 
                rcnt = MAX_LOCAL_QUEUE_LEN - queue->index;
            memmove(queue->buffer+queue->index, arm->rx2 + SIZEOF_HEADER, rcnt);
            queue->index += rcnt;
            return n;
        }
        queue->index = 0;
        cnt -= n;
    }
    int n2 = rcnt < cnt ? rcnt : cnt;
    memmove(str+n, arm->rx2 + SIZEOF_HEADER, n2);
    if (rcnt > cnt) { // str is too short,  move rest of string to local queue
        queue->index = rcnt - cnt;
        if (queue->index > MAX_LOCAL_QUEUE_LEN) queue->index = MAX_LOCAL_QUEUE_LEN;
        memmove(queue->buffer, arm->rx2 + SIZEOF_HEADER+cnt, queue->index);
    }
    return n+n2;
}

int read_qstring(arm_handle* arm, uint8_t uart, uint8_t* str, int cnt)
{
    if (uart > 0) {
        pabort("Bad parameter uart");
        return -1;
    }
    uart_queue* queue = &arm->uart_q[uart];
    
    // join uart_queue and rcnt chars
    int n = queue->index < cnt ? queue->index : cnt;
    if (n > 0) {
        memmove(str, queue->buffer, n);
        if (n < queue->index) {  // str is too short for import local queue
            memmove(queue->buffer, queue->buffer + n, queue->index - n);
            queue->index -= n;
            return n;
        }
        queue->index = 0;
        return n;
    }
    return 0;
}


//const char* GPIO_INT[] = { "27", "23", "22" };
#define START_SPI_SPEED 5000000
int arm_init(arm_handle* arm, const char* device, uint32_t speed, int index, const char* gpio)
{
    arm->fd = open(device, O_RDWR);

    if (arm->fd < 0) {
        pabort("Cannot open device");
        return -1;
    }
    //set_spi_mode(fd,0);
    if (speed==0) {
        set_spi_speed(arm->fd, START_SPI_SPEED);
    } else {
        set_spi_speed(arm->fd, speed);
    }
    arm->index = index;

    int i;
    for (i=0; i< 4; i++) {
       arm->uart_q[i].masterpty = -1;
       arm->uart_q[i].remain = 0;
       arm->uart_q[i].index = 0;
    }
    // Prepare transactional structure
    memset(arm->tr, 0, sizeof(arm->tr));
    arm->tr[0].delay_usecs = nss_pause;    // starting pause between NSS and SCLK
    arm->tr[1].tx_buf = (unsigned long) &arm->tx1;
    arm->tr[1].rx_buf = (unsigned long) &arm->rx1;
    arm->tr[1].len = SNIPLEN1;
    arm->tr[2].tx_buf = (unsigned long) arm->tx2;
    arm->tr[2].rx_buf = (unsigned long) arm->rx2;
    arm->tr[3].tx_buf = (unsigned long) arm->tx2 + _MAX_SPI_RX;
    arm->tr[3].rx_buf = (unsigned long) arm->rx2 + _MAX_SPI_RX;
    arm->tr[4].tx_buf = (unsigned long) arm->tx2 + (_MAX_SPI_RX*2);
    arm->tr[4].rx_buf = (unsigned long) arm->rx2 + (_MAX_SPI_RX*2);
    arm->tr[5].tx_buf = (unsigned long) arm->tx2 + (_MAX_SPI_RX*3);
    arm->tr[5].rx_buf = (unsigned long) arm->rx2 + (_MAX_SPI_RX*3);
    arm->tr[6].tx_buf = (unsigned long) arm->tx2 + (_MAX_SPI_RX*4);
    arm->tr[6].rx_buf = (unsigned long) arm->rx2 + (_MAX_SPI_RX*4);
    /* Load firmware and hardware versions */
    int backup = arm_verbose;
    arm_verbose = 0;
    uint16_t configregs[5];
    if (read_regs(arm, 1000, 5, configregs) == 5)
        parse_version(&arm->bv, configregs);
    //arm_version(arm);
    if (speed == 0) {
        speed = get_board_speed(&arm->bv);
        set_spi_speed(arm->fd, speed);
        if (read_regs(arm, 1000, 5, configregs) != 5) {
            set_spi_speed(arm->fd, START_SPI_SPEED);
            speed = START_SPI_SPEED;
        }
    }
    arm_verbose = backup;
    if (arm->bv.sw_version) {
        if (arm_verbose) 
            printf("Board on %s firmware=%d.%d  hardware=%d.%d (%s) (spi %dMHz)\n", device,
                SW_MAJOR(arm->bv.sw_version), SW_MINOR(arm->bv.sw_version),
                HW_BOARD(arm->bv.hw_version), HW_MAJOR(arm->bv.hw_version),
                arm_name(arm->bv.hw_version), speed / 1000000);
    } else {
        close(arm->fd);
        return -1;
    }

    /* Open fdint for interrupt catcher */
    arm->fdint = -1;

    if ((gpio == NULL)||(strlen(gpio) == 0)||(arm->bv.int_mask_register<=0)) return 0;

    int fdx = open("/sys/class/gpio/export", O_WRONLY);
    if (fdx < 0) return 0;
    write(fdx, gpio, strlen(gpio));
    close(fdx);

    char gpiobuf[256];
    sprintf(gpiobuf, "/sys/class/gpio/gpio%s/edge", gpio);
    fdx = open(gpiobuf, O_WRONLY);
    if (fdx < 0) return 0;
    write(fdx, "rising", 6);
    close(fdx);

    sprintf(gpiobuf, "/sys/class/gpio/gpio%s/value", gpio);
    arm->fdint = open(gpiobuf, O_RDONLY);
    if (arm->fdint < 0) return 0;

    return 0;
}

/***************************************************************************************/

typedef struct {
    arm_handle* arm;
    struct spi_ioc_transfer* tr;
    arm_comm_firmware* tx;
    arm_comm_firmware* rx;
} Tfirmware_context;

int firmware_op(arm_handle* arm, arm_comm_firmware* tx, arm_comm_firmware* rx, int tr_len, struct spi_ioc_transfer* tr)
{
    tx->crc = SpiCrcString((uint8_t*)tx, sizeof(arm_comm_firmware) - sizeof(tx->crc), 0);
    int ret = ioctl(arm->fd, SPI_IOC_MESSAGE(tr_len), tr);
    if (ret < 1) {
        pabort("Can't send firmware-op spi message");
        return -1;
    }
    uint16_t crc = SpiCrcString((uint8_t*)rx, sizeof(arm_comm_firmware) - sizeof(rx->crc),0);
    //printf("a=%0x d=%x crc=%x\n", rx->address, rx->data[0], rx->crc);
    if (crc != rx->crc) {
        pabort("Bad crc in firmware operation");
        return -1;
    }
}

void* start_firmware(arm_handle* arm)
{
    Tfirmware_context* fwctx = calloc(1, sizeof(Tfirmware_context));
    if (fwctx == NULL) return NULL;
    fwctx->arm = arm;
    /* Alloc Tx Rx buffers */
    fwctx->tx = calloc(1, sizeof(arm_comm_firmware)+2);
    if (fwctx->tx == NULL) {
        free(fwctx);
        return NULL;
    }
    fwctx->rx = calloc(1, sizeof(arm_comm_firmware)+2);
    if (fwctx->rx == NULL) { 
        free(fwctx->tx); 
        free(fwctx);
        return NULL; 
    }
    /* Transaction array */
    int i;
    int tr_len = ((sizeof(arm_comm_firmware) - 1) / _MAX_SPI_RX) + 2;               // Transaction array length 
    fwctx->tr = calloc(tr_len, sizeof(struct spi_ioc_transfer));  // Alloc transaction array
    if (fwctx->tr == NULL) {
        free(fwctx->rx); 
        free(fwctx->tx); 
        free(fwctx);
        return NULL; 
    } 
    fwctx->tr[0].delay_usecs = 5;                                                          // first transaction has no data
    for (i=0; i < tr_len-1; i++) {
        fwctx->tr[i+1].len = _MAX_SPI_RX;
        fwctx->tr[i+1].tx_buf = (unsigned long) fwctx->tx + (_MAX_SPI_RX*i);
        fwctx->tr[i+1].rx_buf = (unsigned long) fwctx->rx + (_MAX_SPI_RX*i);
    }
    fwctx->tr[tr_len-1].len = ((sizeof(arm_comm_firmware) - 1) % _MAX_SPI_RX) + 1;       // last transaction is shorter

    int prog_bit = 1004;
    if (arm->bv.sw_version <= 0x400) prog_bit = 104;
    write_bit(arm, prog_bit, 1);                                                   // start programming in ARM
    usleep(100000);
    return (void*) fwctx;
}


void finish_firmware(void*  ctx)
{
    Tfirmware_context* fwctx = (Tfirmware_context*) ctx;
    int tr_len = ((sizeof(arm_comm_firmware) - 1) / _MAX_SPI_RX) + 2;               // Transaction array length 

    fwctx->tx->address = ARM_FIRMWARE_KEY;  // finish transfer
    firmware_op(fwctx->arm, fwctx->tx, fwctx->rx, tr_len, fwctx->tr);
    if (fwctx->rx->address != ARM_FIRMWARE_KEY) {
        if (arm_verbose) printf("UNKNOWN ERROR\nREBOOTING...\n");
    } else {
        if (arm_verbose) printf("REBOOTING...\n");
    }

    // dealloc
    free(fwctx->tr); 
    free(fwctx->rx); 
    free(fwctx->tx);
    free(fwctx); 
    usleep(100000);
}

int send_firmware(void* ctx, uint8_t* data, size_t datalen, uint32_t start_address)
{
    Tfirmware_context* fwctx = (Tfirmware_context*) ctx;
    int tr_len = ((sizeof(arm_comm_firmware) - 1) / _MAX_SPI_RX) + 2;               // Transaction array length 

    int prev_addr = -1;
    int len = datalen;
    uint32_t address = start_address;
    while (len >= 0) {
        fwctx->tx->address = address;
        if (len >= ARM_PAGE_SIZE) {
            memcpy(fwctx->tx->data, data + (address-start_address), ARM_PAGE_SIZE);  // read page from file
            len = len - ARM_PAGE_SIZE;
        } else if (len != 0) {
            memcpy(fwctx->tx->data, data + (address-start_address), len);            // read  page (part) from file
            memset(fwctx->tx->data+len, 0xff, ARM_PAGE_SIZE-len);  
            len = 0;
        } else {
            address = 0xF400;   // read-only page; operation is performed only for last page confirmation
            len = -1;
        }
        firmware_op(fwctx->arm, fwctx->tx, fwctx->rx, tr_len, fwctx->tr);
        if (fwctx->rx->address != ARM_FIRMWARE_KEY) {
            if ((address == prev_addr)||(prev_addr == -1)) { 
                // double error or start error
                usleep(100000);
                break;
            }
            address = prev_addr;
            len = datalen - (address-start_address);
            usleep(100000);
            continue;
        }
        if (prev_addr != -1) if (arm_verbose) printf("%04x OK\n", prev_addr);
        usleep(100000);
        prev_addr = address;
        address = address + ARM_PAGE_SIZE;
    } 
}

int _send_firmware(arm_handle* arm, uint8_t* data, size_t datalen, uint32_t start_address)
{
    /* Alloc Tx Rx buffers */
    arm_comm_firmware* tx = calloc(1, sizeof(arm_comm_firmware)+2);
    if (tx == NULL) return -1;
    arm_comm_firmware* rx = calloc(1, sizeof(arm_comm_firmware)+2);
    if (rx == NULL) { 
        free(tx); 
        return -1; 
    }
    /* Transaction array */
    int i;
    int tr_len = ((sizeof(arm_comm_firmware) - 1) / _MAX_SPI_RX) + 2;               // Transaction array length 
    struct spi_ioc_transfer* tr = calloc(tr_len, sizeof(struct spi_ioc_transfer));  // Alloc transaction array
    if (tr == NULL) {
        free(rx); 
        free(tx); 
        return -1; 
    } 
    tr[0].delay_usecs = 5;                                                          // first transaction has no data
    for (i=0; i < tr_len-1; i++) {
        tr[i+1].len = _MAX_SPI_RX;
        tr[i+1].tx_buf = (unsigned long) tx + (_MAX_SPI_RX*i);
        tr[i+1].rx_buf = (unsigned long) rx + (_MAX_SPI_RX*i);
    }
    tr[tr_len-1].len = ((sizeof(arm_comm_firmware) - 1) % _MAX_SPI_RX) + 1;       // last transaction is shorter

    int prog_bit = 1004;
    if (arm->bv.sw_version <= 0x400) prog_bit = 104;
    write_bit(arm, prog_bit, 1);                                                   // start programming in ARM
    usleep(100000);

    int prev_addr = -1;
    int len = datalen;
    uint32_t address = start_address;

    while (len >= 0) {
        tx->address = address;
        if (len >= ARM_PAGE_SIZE) {
            memcpy(tx->data, data + (address-start_address), ARM_PAGE_SIZE);  // read page from file
            len = len - ARM_PAGE_SIZE;
        } else if (len != 0) {
            memcpy(tx->data, data + (address-start_address), len);            // read  page (part) from file
            memset(tx->data+len, 0xff, ARM_PAGE_SIZE-len);  
            len = 0;
        } else {
            address = 0xF400;   // read-only page; operation is performed only for last page confirmation
            len = -1;
        }
        firmware_op(arm, tx, rx, tr_len, tr);
        if (rx->address != ARM_FIRMWARE_KEY) {
            if ((address == prev_addr)||(prev_addr == -1)) { 
                // double error or start error
                usleep(100000);
                break;
            }
            address = prev_addr;
            len = datalen - (address-start_address);
            usleep(100000);
            continue;
        }
        if (prev_addr != -1) if (arm_verbose) printf("%04x OK\n", prev_addr);
        usleep(100000);
        prev_addr = address;
        address = address + ARM_PAGE_SIZE;
    } 

    tx->address = ARM_FIRMWARE_KEY;  // finish transfer
    firmware_op(arm, tx, rx, tr_len, tr);
    if (rx->address != ARM_FIRMWARE_KEY) {
        if (arm_verbose) printf("UNKNOWN ERROR\nREBOOTING...\n");
    } else {
        if (arm_verbose) printf("REBOOTING...\n");
    }

    // dealloc
    free(tr); 
    free(rx); 
    free(tx);
    usleep(100000);
}
