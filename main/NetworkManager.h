#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "ConfigManager.h"

namespace NetworkManager {



    // 连接状态枚举
    enum class ConnectionState {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        ERROR
    };

    // 通道信息结构
    struct ChannelInfo {
        ConnectionState state;
        WiFiClient wifiClient;
        PubSubClient* mqttClient;
        uint32_t lastHeartbeatTime;
        uint32_t lastReconnectAttempt;
        uint32_t lastConnectionCheck;
        bool needsReconnect;
    };

    void begin();  // 初始化并创建任务
    void loop();   // 内部循环函数
    bool sendToChannel(int channelIndex, const uint8_t* data, size_t length);
    bool sendToSource(const String& source, const uint8_t* data, size_t length);
    bool isChannelConnected(int channelIndex);
    String getChannelStatus(int channelIndex);
    void applyNetworkConfig();
    static void printChannelStatus();


} // namespace NetworkManager