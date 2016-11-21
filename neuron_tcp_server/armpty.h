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
#ifndef __armpty_h
#define __armpty_h


int armpty_open(arm_handle* arm, uint8_t uart);
int armpty_setuart(int masterfd, arm_handle* arm, uint8_t uart);
int armpty_readpty(int masterfd, arm_handle* arm, uint8_t uart);
int armpty_readuart(arm_handle* arm, int do_idle);


#endif