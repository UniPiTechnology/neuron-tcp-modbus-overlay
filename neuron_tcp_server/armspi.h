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
#ifndef __armspi_h
#define __armspi_h

#include <stdint.h>
#include <linux/spi/spidev.h>

// brain/spi.h
// Structures for communication header
typedef struct {
    uint8_t  op;
    uint8_t  len;
    uint16_t reg;
    uint16_t crc;
} __attribute__((packed)) arm_comm_header_crc;


typedef  struct {
    uint8_t  op;
    uint8_t  len;
    uint16_t reg;
} __attribute__((packed)) arm_comm_header;

typedef struct {
    uint8_t  op;
    uint8_t  int_status;
    uint8_t  len;
    uint8_t  ch1;
} __attribute__((packed)) arm_comm_chr_header;


typedef struct {
    uint8_t  op;
    uint8_t  len;
    uint8_t  channel;
    uint8_t  remain;
} __attribute__((packed)) arm_comm_str_header;

#define ARM_PAGE_SIZE      1024
#define ARM_FIRMWARE_KEY   0xAA99FF33
typedef struct {
    uint32_t  address;
    uint8_t   data[ARM_PAGE_SIZE];
    uint16_t  crc;
} __attribute__((packed)) arm_comm_firmware;


// STATIC BUFFERS
#define SIZEOF_HEADER  sizeof(arm_comm_header)     // Header size without CRC
#define SNIPLEN1       sizeof(arm_comm_header_crc) // Header size including CRC
#define SNIPLEN2       256 // Max size of second chunk - DOCISTIT (255?, 256?, 256 + header!!!)
#define CRC_SIZE       2

#define MAX_LOCAL_QUEUE_LEN 256
typedef struct {
    int index;
    uint8_t buffer[MAX_LOCAL_QUEUE_LEN];
    int remain;
    int overflow;
    int masterpty;
} uart_queue;


typedef struct {
    int fd;
    int fdint;
    int index;
    arm_comm_header_crc tx1;
    arm_comm_header_crc rx1;
    uint8_t tx2[SNIPLEN2 + CRC_SIZE + 40];
    uint8_t rx2[SNIPLEN2 + CRC_SIZE + 40];
    struct spi_ioc_transfer tr[7];     // Transaction structure for 5 chunks
    uint8_t sw_version;
    uint8_t sw_subver;
    uint8_t hw_version;
    uint8_t hw_subver;
    uint8_t di_count;
    uint8_t do_count;
    uint8_t ai_count;
    uint8_t ao_count;
    uint8_t uart_count;
    uint8_t uled_count;
    uint16_t int_mask_register;
    uart_queue uart_q[4];              // local queue for uarts on arm
}  arm_handle;


int arm_init(arm_handle* arm, const char* device, uint32_t speed, int index, const char* gpio);
int idle_op(arm_handle* arm);
int read_regs(arm_handle* arm, uint16_t reg, uint8_t cnt, uint16_t* result);
int write_regs(arm_handle* arm, uint16_t reg, uint8_t cnt, uint16_t* values);
int read_bits(arm_handle* arm, uint16_t reg, uint16_t cnt, uint8_t* result);
int write_bit(arm_handle* arm, uint16_t reg, uint8_t value);
int write_bits(arm_handle* arm, uint16_t reg, uint16_t cnt, uint8_t* values);
int write_char(arm_handle* arm, uint8_t uart, uint8_t c);
int write_string(arm_handle* arm, uint8_t uart, uint8_t* str, int len);
int read_string(arm_handle* arm, uint8_t uart, uint8_t* str, int cnt);

const char* arm_name(arm_handle* arm);

int send_firmware(arm_handle* arm, uint8_t* data, size_t datalen, uint32_t start_address);
extern int arm_verbose;

#endif
