#include "TCPProtocol.h"

namespace Protocol {

TCPProtocol::TCPProtocol() {}

TCPProtocol::~TCPProtocol() {
    end();
}

bool TCPProtocol::begin(const ProtocolConfig& cfg) {
    end();
    config = cfg;

    if (!config.enabled) {
        return true;
    }

    Serial.printf("[TCP-%s] Starting TCP protocol to %s:%d\n",
                  config.source.c_str(), config.target.c_str(), config.port);

    return connect();
}

void TCPProtocol::end() {
    if (client.connected()) {
        client.stop();
    }
    config.connected = false;
}

bool TCPProtocol::connect() {
    if (config.target.isEmpty() || config.port == 0) {
        return false;
    }

    Serial.printf("[TCP-%s] Connecting to %s:%d\n",
                  config.source.c_str(), config.target.c_str(), config.port);

    if (client.connect(config.target.c_str(), config.port)) {
        config.connected = true;
        config.lastHeartbeatTime = millis();
        Serial.printf("[TCP-%s] Connected successfully\n", config.source.c_str());

        // 发送注册包
        if (!config.registerPackage.isEmpty()) {
            String regPacket = config.registerPackage;
            sendData((const uint8_t*)regPacket.c_str(), regPacket.length());
        }
        return true;
    } else {
        config.connected = false;
        Serial.printf("[TCP-%s] Connection failed\n", config.source.c_str());
        return false;
    }
}

bool TCPProtocol::sendData(const uint8_t* data, size_t length) {
    if (!isConnected()) {
        return false;
    }

    size_t sent = client.write(data, length);
    bool success = (sent == length);

    if (success) {
        Serial.printf("[TCP-%s] Sent %d bytes\n", config.source.c_str(), length);
    } else {
        Serial.printf("[TCP-%s] Send failed, sent %d/%d bytes\n",
                      config.source.c_str(), sent, length);
    }

    return success;
}

bool TCPProtocol::isConnected() {
    return config.connected && client.connected();
}

void TCPProtocol::loop() {
    if (!config.enabled) return;

    // 检查连接状态
    checkConnection();

    // 处理心跳
    if (isConnected() && config.heartbeatTime > 0) {
        uint32_t currentTime = millis();
        if (currentTime - config.lastHeartbeatTime >= config.heartbeatTime * 1000) {
            sendHeartbeat();
            config.lastHeartbeatTime = currentTime;
        }
    }
}

void TCPProtocol::checkConnection() {
    if (!isConnected()) {
        uint32_t currentTime = millis();
        if (currentTime - config.lastReconnectAttempt >= 5000) { // 5秒重试
            Serial.printf("[TCP-%s] Attempting reconnect...\n", config.source.c_str());
            connect();
            config.lastReconnectAttempt = currentTime;
        }
    }
}

} // namespace Protocol