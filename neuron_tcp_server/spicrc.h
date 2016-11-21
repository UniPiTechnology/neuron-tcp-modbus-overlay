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

#ifndef __spicrce_h
#define __spicrc_h

#include <stdint.h>

uint16_t SpiCrcString(uint8_t* inputstring, int length, uint16_t initval);

#endif
