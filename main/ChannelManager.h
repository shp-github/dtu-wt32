#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "BaseProtocol.h"
#include "TCPProtocol.h"
#include "MQTTProtocol.h"

namespace ChannelManager {

    struct ChannelConfig {
        bool enabled = false;
        String source;              // serial1, serial2, custom1
        String protocol;            // "tcp", "mqtt"
        String target;              // IP地址
        uint16_t port = 0;
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

    void begin();
    void updateFromJson(const JsonVariant &doc);
    void loop();
    bool sendData(int channelIndex, const uint8_t* data, size_t length);
    bool sendData(const String& source, const uint8_t* data, size_t length);
    JsonArray serializeChannels(JsonDocument &doc);
    String getChannelStatus(int index);
    const ChannelConfig& getChannelConfig(int index);

} // namespace ChannelManager