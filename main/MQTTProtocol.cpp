#include "MQTTProtocol.h"

namespace Protocol {

// 静态回调函数
void MQTTProtocol::staticMqttCallback(char* topic, byte* payload, unsigned int length) {
    // 这里可以通过其他方式获取实例，简化处理
    Serial.printf("[MQTT] Received message on topic: %s\n", topic);
}

MQTTProtocol::MQTTProtocol() : mqttClient(wifiClient) {
    mqttClient.setCallback(staticMqttCallback);
}

MQTTProtocol::~MQTTProtocol() {
    end();
}

bool MQTTProtocol::begin(const ProtocolConfig& cfg) {
    end();
    config = cfg;

    if (!config.enabled) {
        return true;
    }

    mqttClient.setServer(config.target.c_str(), config.port);

    Serial.printf("[MQTT-%s] Starting MQTT protocol to %s:%d\n",
                  config.source.c_str(), config.target.c_str(), config.port);

    return connect();
}

void MQTTProtocol::end() {
    if (mqttClient.connected()) {
        mqttClient.disconnect();
    }
    config.connected = false;
}

bool MQTTProtocol::connect() {
    if (config.target.isEmpty() || config.port == 0) {
        return false;
    }

    Serial.printf("[MQTT-%s] Connecting to %s:%d\n",
                  config.source.c_str(), config.target.c_str(), config.port);

    String clientId = config.clientID.isEmpty() ?
        ("WT32-" + String(random(0xffff), HEX)) : config.clientID;

    bool success = mqttClient.connect(
        clientId.c_str(),
        config.username.c_str(),
        config.password.c_str(),
        config.publishTopic.c_str(),  // 遗言主题
        config.QOS,
        config.PubRetain,
        config.lastWillMessage.c_str()
    );

    if (success) {
        config.connected = true;
        config.lastHeartbeatTime = millis();
        Serial.printf("[MQTT-%s] Connected successfully, clientID: %s\n",
                      config.source.c_str(), clientId.c_str());

        // 订阅主题
        if (!config.subscribeTopic.isEmpty()) {
            mqttClient.subscribe(config.subscribeTopic.c_str());
            Serial.printf("[MQTT-%s] Subscribed to: %s\n",
                          config.source.c_str(), config.subscribeTopic.c_str());
        }

        // 发送注册包
        if (!config.registerPackage.isEmpty()) {
            String regPacket = config.registerPackage;
            sendData((const uint8_t*)regPacket.c_str(), regPacket.length());
        }

        return true;
    } else {
        config.connected = false;
        Serial.printf("[MQTT-%s] Connection failed, state: %d\n",
                      config.source.c_str(), mqttClient.state());
        return false;
    }
}

bool MQTTProtocol::sendData(const uint8_t* data, size_t length) {
    if (!isConnected()) {
        return false;
    }

    String message;
    message.concat((const char*)data, length);

    bool success = mqttClient.publish(config.publishTopic.c_str(), message.c_str());

    if (success) {
        Serial.printf("[MQTT-%s] Published to %s: %s\n",
                      config.source.c_str(), config.publishTopic.c_str(), message.c_str());
    } else {
        Serial.printf("[MQTT-%s] Publish failed\n", config.source.c_str());
    }

    return success;
}

bool MQTTProtocol::isConnected() {
    return config.connected && mqttClient.connected();
}

void MQTTProtocol::loop() {
    if (!config.enabled) return;

    mqttClient.loop();

    if (!isConnected()) {
        uint32_t currentTime = millis();
        if (currentTime - config.lastReconnectAttempt >= 5000) { // 5秒重试
            Serial.printf("[MQTT-%s] Attempting reconnect...\n", config.source.c_str());
            connect();
            config.lastReconnectAttempt = currentTime;
        }
    } else {
        // 处理心跳
        if (config.heartbeatTime > 0) {
            uint32_t currentTime = millis();
            if (currentTime - config.lastHeartbeatTime >= config.heartbeatTime * 1000) {
                sendHeartbeat();
                config.lastHeartbeatTime = currentTime;
            }
        }
    }
}

} // namespace Protocol