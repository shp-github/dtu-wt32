#pragma once
#include <Arduino.h>

namespace SerialManager {

    enum class ParityType { NONE = 0, EVEN, ODD };

    struct UartConfig {
        bool enabled = false;
        int baud = 9600;
        int dataBits = 8;
        int stopBits = 1;
        ParityType parity = ParityType::NONE;
        String name = "";
    };

    extern HardwareSerial Serial1;
    extern HardwareSerial Serial2;

    void init();
    void applyConfig();

} // namespace SerialManager
