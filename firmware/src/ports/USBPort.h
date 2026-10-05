// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// USBPort.h — one downstream USB-C port.
//
// Each port has three signals:
//   power enable    an XL9555 expander output driving the TPS2552 load switch.
//                   Active LOW: LOW = powered.
//   fault           an XL9555 expander input from the TPS2552. Active LOW:
//                   LOW = over-current.
//   device sense    an ESP32-S3 GPIO, pulled up. A plugged-in device pulls it
//                   LOW — this works with the port's 5V off.
//
// Nothing here polls. A plug or unplug raises an interrupt on the sense GPIO;
// a fault change raises the expander's shared INT line. The interrupts only
// set flags — all I2C work happens in the main loop through serviceSense()
// and applyFault().
#pragma once
#include <Arduino.h>

class USBPort
{
public:
    // index is 0-5; the pins are the ESP32 sense GPIO and the two expander pins.
    USBPort(uint8_t index, uint8_t sensePin, uint8_t enExpPin, uint8_t faultExpPin);

    // Set up the pins (port powered OFF) and arm the sense interrupt. Call after
    // Wire and the expander (ioex) are running.
    void begin();

    // ---- Power ----
    void enable() { setEnabled(true); }
    void disable() { setEnabled(false); }
    void setEnabled(bool on);
    bool isEnabled() const { return _enabled; }

    // ---- Device sense ----
    // True when a plug/unplug edge is waiting to be handled.
    bool senseDirty() const { return _senseDirty; }
    // Clear the flag and re-read the line. Returns true if presence changed.
    // Main loop only, never from an interrupt.
    bool serviceSense();
    bool isOccupied() const { return _occupied; }

    // ---- Fault ----
    // Pick this port's fault bit out of the two expander input registers
    // (reg0 = P0..P7, reg1 = P8..P15). Returns true if the fault state changed.
    bool applyFault(uint8_t reg0, uint8_t reg1);
    bool hasFault() const { return _fault; }

private:
    static void IRAM_ATTR senseISR(void *arg);

    uint8_t _index;       // 0-5
    uint8_t _sensePin;    // ESP32-S3 GPIO
    uint8_t _enExpPin;    // expander pin: power enable
    uint8_t _faultExpPin; // expander pin: fault

    volatile bool _senseDirty = false;
    bool _enabled = false;
    bool _occupied = false;
    bool _fault = false;
};
