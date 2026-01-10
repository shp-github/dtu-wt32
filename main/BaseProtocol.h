#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace Protocol {

    enum class ProtocolType {
        TCP,
        MQTT,
        UDP
    };

    struct ProtocolConfig {
        bool enabled = false;
        String source;              // 数据来源 serial1, serial2, custom
        ProtocolType protocol;      // 协议类型
        String target;              // 目标地址
        uint16_t port = 0;          // 端口
        int heartbeatTime = 30;     // 心跳间隔(秒)
        String username;
        String password;
        String registerPackage;     // 注册包
        String heartbeatPackage;    // 心跳包
        String subscribeTopic;      // MQTT订阅主题
        String publishTopic;        // MQTT发布主题
        String clientID;            // MQTT客户端ID
        int QOS = 0;                // MQTT QOS
        bool PubRetain = false;     // MQTT保留消息
        String lastWillMessage;     // MQTT遗言

        // 状态信息
        bool connected = false;
        uint32_t lastHeartbeatTime = 0;
        uint32_t lastReconnectAttempt = 0;
    };

    class BaseProtocol {
    public:
        virtual ~BaseProtocol() = default;

        virtual bool begin(const ProtocolConfig& config) = 0;
        virtual void end() = 0;
        virtual bool sendData(const uint8_t* data, size_t length) = 0;
        virtual bool isConnected() = 0;
        virtual void loop() = 0;  // 处理心跳、重连等

        const ProtocolConfig& getConfig() const { return config; }
        String getStatusString() const;

    protected:
        ProtocolConfig config;
        virtual void sendHeartbeat();
    };

} // namespace Protocol