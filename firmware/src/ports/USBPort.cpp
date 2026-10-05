// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// USBPort.cpp — one downstream port's power, fault and device-sense handling.
#include "USBPort.h"
#include "UM_LCA9555.h"

USBPort::USBPort(uint8_t index, uint8_t sensePin, uint8_t enExpPin, uint8_t faultExpPin)
    : _index(index), _sensePin(sensePin), _enExpPin(enExpPin), _faultExpPin(faultExpPin) {}

// Sense-line interrupt: just note that the line moved.
void IRAM_ATTR USBPort::senseISR(void *arg) { static_cast<USBPort *>(arg)->_senseDirty = true; }

void USBPort::begin()
{
    // Sense: pulled up, interrupt on either edge. Stays armed whatever the
    // power state, since detection works with the port off.
    pinMode(_sensePin, INPUT_PULLUP);
    attachInterruptArg(digitalPinToInterrupt(_sensePin), senseISR, this, CHANGE);

    // Enable is active LOW, so start HIGH = port off.
    ioex.pin_mode(_enExpPin, OUTPUT, HIGH);
    ioex.pin_mode(_faultExpPin, INPUT);

    _enabled = false;
    _occupied = (digitalRead(_sensePin) == LOW);
}

void USBPort::setEnabled(bool on)
{
    ioex.write(_enExpPin, on ? LOW : HIGH); // active LOW
    _enabled = on;
}

bool USBPort::serviceSense()
{
    _senseDirty = false;
    bool occ = (digitalRead(_sensePin) == LOW); // LOW = device present
    bool changed = (occ != _occupied);
    _occupied = occ;
    return changed;
}

bool USBPort::applyFault(uint8_t reg0, uint8_t reg1)
{
    uint8_t reg = (_faultExpPin < 8) ? reg0 : reg1;
    bool fault = ((reg >> (_faultExpPin & 0x07)) & 0x01) == 0; // active LOW
    bool changed = (fault != _fault);
    _fault = fault;
    return changed;
}
