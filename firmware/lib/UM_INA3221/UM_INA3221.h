// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2025 Unexpected Maker
//
// This library is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License, version 3 or (at your
// option) any later version. See the LICENSE file in this folder.
/*
  Arduino Library for the INA3221 3-channel voltage/current/power monitor

  Monitoring only — bus voltage, shunt voltage, current and power per channel.
  One object per chip. Channels are addressed 1..3 (the chip's CH1..CH3); the
  board-specific channel<->port mapping lives in the application, not here.
*/

#pragma once

#include <Wire.h>
#include <Arduino.h>

// I2C addresses, selected by the A0 strap pin
#define INA3221_ADDR_GND 0x40 // A0 -> GND
#define INA3221_ADDR_VS  0x41 // A0 -> VS (3V3)
#define INA3221_ADDR_SDA 0x42 // A0 -> SDA
#define INA3221_ADDR_SCL 0x43 // A0 -> SCL
#define INA3221_DEF_ADDRESS INA3221_ADDR_GND

// Error codes
#define INA3221_OK 0x00
#define INA3221_CHANNEL_ERROR 0x81
#define INA3221_I2C_ERROR 0x82

// Registers (all 16-bit, MSB first)
#define INA3221_REG_CONFIG 0x00
#define INA3221_REG_SHUNT_CH1 0x01 // shunt = 0x01 + (ch-1)*2
#define INA3221_REG_BUS_CH1 0x02   // bus   = 0x02 + (ch-1)*2
#define INA3221_REG_MANUF_ID 0xFE
#define INA3221_REG_DIE_ID 0xFF

// Identity
#define INA3221_MANUF_ID 0x5449 // "TI"
#define INA3221_DIE_ID 0x3220

// Default config: all 3 channels on, avg=1, 1.1ms bus+shunt conv, continuous.
#define INA3221_DEF_CONFIG 0x7127

// Register LSBs (from the datasheet)
#define INA3221_SHUNT_LSB_mV 0.04f // 40 uV per LSB
#define INA3221_BUS_LSB_V 0.008f   // 8 mV per LSB

// Fallback shunt value (ohms) until the application sets the real one.
#define INA3221_DEFAULT_SHUNT_OHMS 0.1f

class INA3221
{
	public:
		// Initialize with I2C address + TwoWire. Returns false if the device
		// doesn't ACK or its manufacturer ID doesn't match. Writes the default
		// (continuous, all-channel) config on success.
		bool begin(uint8_t address = INA3221_DEF_ADDRESS, TwoWire *wire = &Wire);
		// Does the device ACK on the bus?
		bool connected();

		// Identity registers (use to confirm a real INA3221 is present).
		uint16_t manufacturer_id();
		uint16_t die_id();

		// Shunt resistance per channel (ohms). Needed for current/power.
		void set_shunt_resistor(uint8_t channel, float ohms);
		float get_shunt_resistor(uint8_t channel);

		// Per-channel measurements (channel 1..3). Return NAN on bad channel.
		float shunt_voltage_mV(uint8_t channel); // voltage across the shunt
		float bus_voltage_V(uint8_t channel);    // bus (load) voltage
		float current_mA(uint8_t channel);       // shunt_mV / shunt_ohms
		float current_A(uint8_t channel);
		float power_mW(uint8_t channel); // bus_V * current_mA

		// Raw config access + soft reset.
		uint16_t read_config();
		bool write_config(uint16_t cfg);
		bool reset();

		uint8_t last_error() const;

	private:
		TwoWire *_wire = &Wire;
		uint8_t _address = INA3221_DEF_ADDRESS;
		uint8_t _error = INA3221_OK;
		float _shunt[3] = {INA3221_DEFAULT_SHUNT_OHMS, INA3221_DEFAULT_SHUNT_OHMS,
		                   INA3221_DEFAULT_SHUNT_OHMS};

		bool valid_channel(uint8_t channel);
		uint16_t i2c_read16(uint8_t reg);
		bool i2c_write16(uint8_t reg, uint16_t value);
};
