// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
#pragma once
#include <Arduino.h>

class SerialApi
{
public:
    static constexpr size_t MAX_REQUEST = 8192;
    static constexpr uint8_t QUEUE_LENGTH = 4;

    bool queue(const String &line);
    void service();

private:
    void run(const String &line);
    String _queue[QUEUE_LENGTH];
    uint8_t _head = 0;
    uint8_t _count = 0;
};

extern SerialApi serialApi;
