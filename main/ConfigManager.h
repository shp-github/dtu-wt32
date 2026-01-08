#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "SerialManager.h"
#include "ModbusManager.h"

namespace ConfigManager {


    struct ChannelConfig {
        bool enabled = false;
        String protocol;
        String target;
        uint16_t port = 0;
        String source;              // serial1, serial2, custom1
        int heartbeatTime = 30;
        String username;
        String password;
        String registerPackage;
        String heartbeatPackage;
        String subscribeTopic;
        String publishTopic;
        String clientID;
        int QOS = 0;
        bool PubRetain = false;
        String lastWillMessage;
    };

    struct DeviceConfig {

        String name;
        String ip = "0.0.0.0";
        String subnet = "255.255.255.0";
        String gateway = "0.0.0.0";
        String dns = "0.0.0.0";
        int isStatic = 0;  // 0=DHCP, 1=静态IP

        int heart_interval = 20;
        ChannelConfig channels[3];
        SerialManager::UartConfig uart1;
        SerialManager::UartConfig uart2;
        ModbusManager::ModbusConfig modbus;
    };


    extern DeviceConfig deviceConfig;

    void begin();
    void saveModule(const String &flag);
    void updateModuleFromJson(const JsonVariant &doc);
    DeviceConfig& getConfig();
    JsonObject serializeModule(JsonDocument &doc, const String &flag);
    void applyConfig();

} // namespace ConfigManager