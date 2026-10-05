// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2025 Unexpected Maker
//
// This library is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License, version 3 or (at your
// option) any later version. See the LICENSE file in this folder.
/*
  Arduino Library for the LCA9555
  Copyright (c) 2025 Unexpected Maker
*/

#pragma once

#include <Wire.h>
#include <Arduino.h>
#include <string.h>

// Default I2C address range
#define LCA9555_ADDR_MIN 0x20
#define LCA9555_ADDR_MAX 0x27
#define LCA9555_DEF_ADDRESS 0x20

// Pin modes: uses Arduino's INPUT/OUTPUT/HIGH/LOW macros.
// pin_mode() treats INPUT (1) as input and any other mode value as output,
// so the ESP32 core's OUTPUT (0x03) works without redefining it here.

// Error codes
#define LCA9555_OK 0x00
#define LCA9555_PIN_ERROR 0x81
#define LCA9555_I2C_ERROR 0x82
#define LCA9555_VALUE_ERROR 0x83

// Register addresses
#define REG_INPUT_PORT0 0x00
#define REG_INPUT_PORT1 0x01
#define REG_OUTPUT_PORT0 0x02
#define REG_OUTPUT_PORT1 0x03
#define REG_POLARITY_PORT0 0x04
#define REG_POLARITY_PORT1 0x05
#define REG_CONFIG_PORT0 0x06
#define REG_CONFIG_PORT1 0x07

class LCA9555
{
	public:
		// Initialize with I2C address and TwoWire instance; returns false if no ACK
		bool begin(uint8_t address = LCA9555_DEF_ADDRESS, TwoWire *wire = &Wire);
		// Check device presence
		bool connected();
		// Configure pin
		bool pin_mode(uint8_t pin, uint8_t mode, uint8_t value = LOW);
		// Set output level
		bool write(uint8_t pin, uint8_t value);
		// Read input level
		int read(uint8_t pin);
		// Read a full 8-bit input register in one I2C transaction.
		// port: 0 = pins P0..P7, 1 = pins P8..P15. Reading the input
		// register(s) also clears the device's INT assertion.
		uint8_t read_port(uint8_t port);
		// Invert input polarity
		bool set_polarity(uint8_t pin, uint8_t polarity);
		// Read polarity setting
		uint8_t get_polarity(uint8_t pin);
		// Last error code
		uint8_t last_error() const;

	private:
		TwoWire *_wire;
		uint8_t _address;
		uint8_t _error;

		// Register cache (indices 0x00..0x07)
		uint8_t _reg_cache[8];
		bool _cache_valid[8];

		// Read-once then cache
		uint8_t read_cached(uint8_t reg);
		// Raw I2C transfers
		uint8_t i2c_read(uint8_t reg);
		bool i2c_write(uint8_t reg, uint8_t value);
};

extern LCA9555 ioex;
