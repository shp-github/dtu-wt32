#include "CustomChannelAdapter.h"
#include "NetworkManager.h"
#include "ConfigManager.h"


namespace CustomChannelAdapter {

// 常量定义
const int CUSTOM_CHANNEL_INDEX = 0;
const size_t BUFFER_SIZE = 2048;

// 静态变量定义
MessageCallback messageCallback = nullptr;
std::vector<String> messageQueue;
bool initialized = false;

void begin() {
    if (initialized) return;

    Serial.println("[CustomChannelAdapter] 初始化自定义通道适配器");

    // 检查通道配置
    ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[CUSTOM_CHANNEL_INDEX];
    Serial.printf("[CustomChannelAdapter] 通道配置: 启用=%d, 协议=%s, 目标=%s:%d, 数据源=%s\n",
                  config.enabled, config.protocol.c_str(), config.target.c_str(),
                  config.port, config.source.c_str());

    if (!config.enabled) {
        Serial.println("[CustomChannelAdapter] 警告: 自定义通道未启用!");
    }

    initialized = true;
    Serial.println("[CustomChannelAdapter] 初始化完成");
}

void setMessageCallback(MessageCallback callback) {
    messageCallback = callback;
    Serial.println("[CustomChannelAdapter] 消息回调已设置");
}

bool publish(const String& topic, const String& payload) {
    if (!initialized) {
        Serial.println("[CustomChannelAdapter] 错误: 适配器未初始化");
        return false;
    }

    if (!NetworkManager::isChannelConnected(CUSTOM_CHANNEL_INDEX)) {
        Serial.println("[CustomChannelAdapter] 错误: 自定义通道未连接");
        return false;
    }

    //String fullMessage = "{\"topic\":\"" + topic + "\",\"payload\":" + payload + "}";
    String fullMessage = payload;

    bool success = NetworkManager::sendToChannel(CUSTOM_CHANNEL_INDEX,
                                                (const uint8_t*)fullMessage.c_str(),
                                                fullMessage.length());

    if (success) {
        Serial.printf("[CustomChannelAdapter] ✓ 发送消息到自定义通道: %s\n", fullMessage.c_str());
    } else {
        Serial.printf("[CustomChannelAdapter] ✗ 发送消息失败\n");
    }

    return success;
}

bool readMessage(String& message) {
    if (messageQueue.empty()) {
        return false;
    }

    message = messageQueue.front();
    messageQueue.erase(messageQueue.begin());

    Serial.printf("[CustomChannelAdapter] 读取消息: %s\n", message.c_str());
    return true;
}

bool hasMessageAvailable() {
    return !messageQueue.empty();
}

void handleNetworkData(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) {
        return;
    }

    String message;
    message.concat((const char*)data, length);

    Serial.printf("[CustomChannelAdapter] 收到网络数据: %s\n", message.c_str());

    messageQueue.push_back(message);

    if (messageQueue.size() > 10) {
        messageQueue.erase(messageQueue.begin());
        Serial.println("[CustomChannelAdapter] 警告: 消息队列已满，丢弃最旧消息");
    }

    if (messageCallback != nullptr) {
        messageCallback(message);
    }
}

void loop() {
    static uint32_t lastCleanup = 0;
    uint32_t currentTime = millis();

    if (currentTime - lastCleanup >= 60000 && !messageQueue.empty()) {
        messageQueue.clear();
        Serial.println("[CustomChannelAdapter] 清理消息队列");
        lastCleanup = currentTime;
    }
}

} // namespace CustomChannelAdapter