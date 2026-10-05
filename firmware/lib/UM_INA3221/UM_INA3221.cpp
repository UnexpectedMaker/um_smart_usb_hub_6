// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2025 Unexpected Maker
//
// This library is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License, version 3 or (at your
// option) any later version. See the LICENSE file in this folder.
/*
  Arduino Library for the INA3221 3-channel voltage/current/power monitor
*/

#include "UM_INA3221.h"

bool INA3221::begin(uint8_t address, TwoWire *wire)
{
	_address = address;
	_wire = wire;
	_error = INA3221_OK;

	if (!connected())
		return false;

	// Confirm it's actually an INA3221, not just something that ACKs.
	if (manufacturer_id() != INA3221_MANUF_ID)
	{
		_error = INA3221_I2C_ERROR;
		return false;
	}

	return write_config(INA3221_DEF_CONFIG);
}

bool INA3221::connected()
{
	_wire->beginTransmission(_address);
	uint8_t rc = _wire->endTransmission();
	_error = (rc == 0 ? INA3221_OK : INA3221_I2C_ERROR);
	return (rc == 0);
}

bool INA3221::valid_channel(uint8_t channel)
{
	if (channel >= 1 && channel <= 3)
		return true;
	_error = INA3221_CHANNEL_ERROR;
	return false;
}

uint16_t INA3221::i2c_read16(uint8_t reg)
{
	_wire->beginTransmission(_address);
	_wire->write(reg);
	if (_wire->endTransmission(false) != 0)
	{
		_error = INA3221_I2C_ERROR;
		return 0;
	}
	if (_wire->requestFrom(_address, (uint8_t)2) != 2)
	{
		_error = INA3221_I2C_ERROR;
		return 0;
	}
	uint8_t hi = _wire->read();
	uint8_t lo = _wire->read();
	_error = INA3221_OK;
	return ((uint16_t)hi << 8) | lo;
}

bool INA3221::i2c_write16(uint8_t reg, uint16_t value)
{
	_wire->beginTransmission(_address);
	_wire->write(reg);
	_wire->write((uint8_t)(value >> 8));
	_wire->write((uint8_t)(value & 0xFF));
	uint8_t rc = _wire->endTransmission();
	_error = (rc == 0 ? INA3221_OK : INA3221_I2C_ERROR);
	return (rc == 0);
}

uint16_t INA3221::manufacturer_id() { return i2c_read16(INA3221_REG_MANUF_ID); }
uint16_t INA3221::die_id() { return i2c_read16(INA3221_REG_DIE_ID); }

void INA3221::set_shunt_resistor(uint8_t channel, float ohms)
{
	if (valid_channel(channel))
		_shunt[channel - 1] = ohms;
}

float INA3221::get_shunt_resistor(uint8_t channel)
{
	return valid_channel(channel) ? _shunt[channel - 1] : NAN;
}

float INA3221::shunt_voltage_mV(uint8_t channel)
{
	if (!valid_channel(channel))
		return NAN;
	uint8_t reg = INA3221_REG_SHUNT_CH1 + (channel - 1) * 2;
	// 13-bit signed value held in bits [15:3]; bottom 3 bits read as 0.
	int16_t raw = (int16_t)i2c_read16(reg);
	return (raw >> 3) * INA3221_SHUNT_LSB_mV;
}

float INA3221::bus_voltage_V(uint8_t channel)
{
	if (!valid_channel(channel))
		return NAN;
	uint8_t reg = INA3221_REG_BUS_CH1 + (channel - 1) * 2;
	int16_t raw = (int16_t)i2c_read16(reg);
	return (raw >> 3) * INA3221_BUS_LSB_V;
}

float INA3221::current_mA(uint8_t channel)
{
	if (!valid_channel(channel))
		return NAN;
	float r = _shunt[channel - 1];
	if (r <= 0.0f)
		return NAN;
	// mV / ohms = mA
	return shunt_voltage_mV(channel) / r;
}

float INA3221::current_A(uint8_t channel) { return current_mA(channel) / 1000.0f; }

float INA3221::power_mW(uint8_t channel)
{
	if (!valid_channel(channel))
		return NAN;
	// V * mA = mW
	return bus_voltage_V(channel) * current_mA(channel);
}

uint16_t INA3221::read_config() { return i2c_read16(INA3221_REG_CONFIG); }
bool INA3221::write_config(uint16_t cfg) { return i2c_write16(INA3221_REG_CONFIG, cfg); }

bool INA3221::reset()
{
	// Bit 15 = soft reset; restores the power-on default config.
	return write_config(0x8000);
}

uint8_t INA3221::last_error() const { return _error; }
