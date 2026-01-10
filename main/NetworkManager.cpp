#include "NetworkManager.h"
#include "SerialManager.h"
#include "ConfigManager.h"
#include "NetManager.h"

using namespace NetManager;

namespace NetworkManager {

static ChannelInfo channels[3];
static bool initialized = false;
// 通道数
static int MAX_CHANNELS = 3;

// 增加缓冲区大小
#define BUFFER_SIZE 1024
// 等待接收数据的间隔 ms
#define TASK_DELAY_MS 10

// 内部函数声明
static void printChannelStatus();

// 内部任务函数
static void taskNetworkChannel(void *parameter) {
  Serial.println("[TASK] 网络通道管理任务启动");

  while (true) {
    NetworkManager::loop();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// 串口数据转发任务（串口→网络）- 已优化
static void taskSerialToNetwork(void *parameter) {
    int serialNum = (int)parameter;
    String taskName = "Serial" + String(serialNum);
    Serial.printf("[TASK] %s 数据转发任务启动（串口→网络）\n", taskName.c_str());

    while (true) {
        HardwareSerial* serial = (serialNum == 1) ? &SerialManager::Serial1 : &SerialManager::Serial2;
        String sourceName = "serial" + String(serialNum);

        // 使用非阻塞方式快速读取所有可用数据
        if (serial->available()) {
            uint8_t buffer[BUFFER_SIZE];
            size_t bytesRead = 0;

            // 快速读取所有可用数据
            while (serial->available() && bytesRead < BUFFER_SIZE) {
                buffer[bytesRead++] = serial->read();
            }

            if (bytesRead > 0) {
                bool success = NetworkManager::sendToSource(sourceName, buffer, bytesRead);

                if (success) {
                    Serial.printf("[%s] 转发 %d 字节到网络\n", taskName.c_str(), bytesRead);
                } else {
                    Serial.printf("[%s] 转发失败，网络通道未连接\n", taskName.c_str());
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
    }
}


static void forwardToDestination(const String& destination, const uint8_t* data, size_t length) {
    if (destination == "serial1") {
        SerialManager::Serial1.write(data, length);
        Serial.printf("[→Serial1] 转发 %d 字节\n", length);
    } else if (destination == "serial2") {
        SerialManager::Serial2.write(data, length);
        Serial.printf("[→Serial2] 转发 %d 字节\n", length);
    }
}

// 优化的网络数据转发任务（网络→串口）
static void taskNetworkToSerial(void *parameter) {
    Serial.println("[TASK] 网络到串口数据转发任务启动");

    while (true) {
        // 定期打印堆栈剩余空间
        static uint32_t lastStackCheck = 0;
        if (millis() - lastStackCheck > 30000) {
            Serial.printf("[TASK] %s 剩余堆栈: %u bytes\n", pcTaskGetName(NULL), uxTaskGetStackHighWaterMark(NULL));
            lastStackCheck = millis();
        }

        for (int i = 0; i < MAX_CHANNELS; i++) {
            ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[i];

            if (!config.enabled || channels[i].state != ConnectionState::CONNECTED) {
                continue;
            }

            // TCP 数据接收
            if (config.protocol == "tcp") {
                WiFiClient& client = channels[i].wifiClient;

                // 使用非阻塞方式快速读取所有TCP数据
                while (client.available() > 0) {
                    uint8_t buffer[BUFFER_SIZE];

                    // 读取所有可用数据，但不阻塞
                    size_t bytesRead = 0;
                    while (client.available() && bytesRead < BUFFER_SIZE) {
                        int byteRead = client.read();
                        if (byteRead >= 0) {
                            buffer[bytesRead++] = (uint8_t)byteRead;
                        } else {
                            break;
                        }
                    }

                    if (bytesRead > 0) {
                        forwardToDestination(config.source, buffer, bytesRead);
                    }
                }
            }

            // MQTT 数据接收在回调函数中处理
        }
        vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
    }
}

// 优化的MQTT回调函数
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    Serial.printf("[MQTT] 收到主题 %s 的消息，长度: %d\n", topic, length);

    // 查找是哪个通道的MQTT客户端
    for (int i = 0; i < MAX_CHANNELS; i++) {
        ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[i];

        if (config.enabled && config.protocol == "mqtt" &&
            channels[i].mqttClient != nullptr &&
            String(topic) == config.subscribeTopic) {
            // 立即转发到对应目标
            forwardToDestination(config.source, payload, length);
            break;
        }
    }
}

// 网络状态监控任务
static void taskNetworkStatus(void *parameter) {
    Serial.println("[TASK] 网络状态监控任务启动");

    while (true) {
        printChannelStatus();
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void begin() {
    if (initialized) return;

    Serial.println("[NetworkManager] 初始化中...");

    for (int i = 0; i < MAX_CHANNELS; i++) {
        channels[i].state = ConnectionState::DISCONNECTED;
        channels[i].mqttClient = nullptr;
        channels[i].lastHeartbeatTime = 0;
        channels[i].lastReconnectAttempt = 0;
        channels[i].lastConnectionCheck = 0;
        channels[i].needsReconnect = false;
    }

    applyNetworkConfig();

    // 创建任务
    xTaskCreatePinnedToCore(taskNetworkChannel, "NetworkChannel", 8192, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(taskSerialToNetwork, "Serial1ToNet", 4096, (void*)1, 4, NULL, 1);
    xTaskCreatePinnedToCore(taskSerialToNetwork, "Serial2ToNet", 4096, (void*)2, 4, NULL, 1);
    xTaskCreatePinnedToCore(taskNetworkToSerial, "NetToSerial", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(taskNetworkStatus, "NetworkStatus", 2048, NULL, 1, NULL, 1);

    initialized = true;
    Serial.println("[NetworkManager] 初始化完成，任务已创建");
}

// 优化的TCP连接函数
bool connectTCP(int channelIndex) {
    ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[channelIndex];

    Serial.printf("[NetworkManager] TCP 通道 %d 配置: 启用=%d, 目标=%s, 端口=%d, 数据源=%s\n",
                  channelIndex, config.enabled, config.target.c_str(), config.port, config.source.c_str());

    if (!config.enabled) {
        Serial.printf("[NetworkManager] 通道 %d 已禁用\n", channelIndex);
        channels[channelIndex].state = ConnectionState::DISCONNECTED;
        return false;
    }

    if (config.target.isEmpty() || config.port == 0) {
        Serial.printf("[NetworkManager] 通道 %d 目标地址或端口无效\n", channelIndex);
        channels[channelIndex].state = ConnectionState::DISCONNECTED;
        return false;
    }

    channels[channelIndex].state = ConnectionState::CONNECTING;

    Serial.printf("[TCP-%s] 正在连接到 %s:%d\n",
                  config.source.c_str(), config.target.c_str(), config.port);

    // 设置连接超时
    channels[channelIndex].wifiClient.setTimeout(5000);

    bool connected = channels[channelIndex].wifiClient.connect(config.target.c_str(), config.port);

    if (connected) {
        // 设置TCP No Delay以减少延迟
        channels[channelIndex].wifiClient.setNoDelay(true);

        channels[channelIndex].state = ConnectionState::CONNECTED;
        channels[channelIndex].lastHeartbeatTime = millis();
        channels[channelIndex].lastConnectionCheck = millis();
        channels[channelIndex].needsReconnect = false;

        Serial.printf("[TCP-%s] ✓ 连接成功\n", config.source.c_str());

        // 发送注册包
        if (!config.registerPackage.isEmpty()) {
            size_t sent = channels[channelIndex].wifiClient.write(
                (const uint8_t*)config.registerPackage.c_str(),
                config.registerPackage.length()
            );
            channels[channelIndex].wifiClient.flush(); // 立即发送
            Serial.printf("[TCP-%s] 发送注册包: %d 字节\n", config.source.c_str(), sent);
        }

        return true;
    } else {
        channels[channelIndex].state = ConnectionState::ERROR;
        channels[channelIndex].needsReconnect = true;
        Serial.printf("[TCP-%s] ✗ 连接失败\n", config.source.c_str());
        return false;
    }
}

bool connectMQTT(int channelIndex) {
    ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[channelIndex];

    Serial.printf("[NetworkManager] MQTT 通道 %d 配置: 启用=%d, 目标=%s, 端口=%d, 数据源=%s\n",
                  channelIndex, config.enabled, config.target.c_str(), config.port, config.source.c_str());

    if (!config.enabled) {
        Serial.printf("[NetworkManager] 通道 %d 已禁用\n", channelIndex);
        channels[channelIndex].state = ConnectionState::DISCONNECTED;
        return false;
    }

    if (config.target.isEmpty() || config.port == 0) {
        Serial.printf("[NetworkManager] 通道 %d 目标地址或端口无效\n", channelIndex);
        channels[channelIndex].state = ConnectionState::DISCONNECTED;
        return false;
    }

    // 创建 MQTT 客户端（如果需要）
    if (channels[channelIndex].mqttClient == nullptr) {
        channels[channelIndex].mqttClient = new PubSubClient(channels[channelIndex].wifiClient);
        channels[channelIndex].mqttClient->setServer(config.target.c_str(), config.port);
        channels[channelIndex].mqttClient->setCallback(mqttCallback);
    }

    channels[channelIndex].state = ConnectionState::CONNECTING;

    Serial.printf("[MQTT-%s] 正在连接到 %s:%d\n",
                  config.source.c_str(), config.target.c_str(), config.port);


    bool success = channels[channelIndex].mqttClient->connect(
        clientId.c_str(),
        config.username.c_str(),
        config.password.c_str(),
        config.publishTopic.c_str(),
        config.QOS,
        config.PubRetain,
        config.lastWillMessage.c_str()
    );

    if (success) {
        channels[channelIndex].state = ConnectionState::CONNECTED;
        channels[channelIndex].lastHeartbeatTime = millis();
        channels[channelIndex].lastConnectionCheck = millis();
        channels[channelIndex].needsReconnect = false;

        Serial.printf("[MQTT-%s] ✓ 连接成功, 客户端ID: %s\n",
                      config.source.c_str(), clientId.c_str());

        // 订阅主题
        if (!config.subscribeTopic.isEmpty()) {
            channels[channelIndex].mqttClient->subscribe(config.subscribeTopic.c_str());
            Serial.printf("[MQTT-%s] 已订阅主题: %s\n",
                          config.source.c_str(), config.subscribeTopic.c_str());
        }

        // 发送注册包
        if (!config.registerPackage.isEmpty()) {
            channels[channelIndex].mqttClient->publish(config.publishTopic.c_str(),
                                                     config.registerPackage.c_str());
            Serial.printf("[MQTT-%s] 发送注册包\n", config.source.c_str());
        }

        return true;
    } else {
        channels[channelIndex].state = ConnectionState::ERROR;
        channels[channelIndex].needsReconnect = true;
        Serial.printf("[MQTT-%s] ✗ 连接失败, 状态: %d\n",
                      config.source.c_str(), channels[channelIndex].mqttClient->state());
        return false;
    }
}

void disconnectChannel(int channelIndex) {
    if (channels[channelIndex].wifiClient.connected()) {
        channels[channelIndex].wifiClient.stop();
        Serial.printf("[NetworkManager] 通道 %d TCP 连接已关闭\n", channelIndex);
    }

    if (channels[channelIndex].mqttClient != nullptr) {
        if (channels[channelIndex].mqttClient->connected()) {
            channels[channelIndex].mqttClient->disconnect();
            Serial.printf("[NetworkManager] 通道 %d MQTT 连接已关闭\n", channelIndex);
        }
        delete channels[channelIndex].mqttClient;
        channels[channelIndex].mqttClient = nullptr;
    }

    channels[channelIndex].state = ConnectionState::DISCONNECTED;
    channels[channelIndex].needsReconnect = false;
}

void sendHeartbeat(int channelIndex) {
    ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[channelIndex];

    if (!config.heartbeatPackage.isEmpty()) {
        bool success = false;

        if (config.protocol == "tcp") {
            if (channels[channelIndex].wifiClient.connected()) {
                size_t sent = channels[channelIndex].wifiClient.write(
                    (const uint8_t*)config.heartbeatPackage.c_str(),
                    config.heartbeatPackage.length()
                );
                success = (sent == config.heartbeatPackage.length());
                if (success) {
                    Serial.printf("[通道 %d] ✓ 通过 TCP 发送心跳包: %d 字节\n",
                                  channelIndex, sent);
                } else {
                    Serial.printf("[通道 %d] ✗ 通过 TCP 发送心跳包失败\n", channelIndex);
                }
            }
        } else if (config.protocol == "mqtt") {
            if (channels[channelIndex].mqttClient != nullptr &&
                channels[channelIndex].mqttClient->connected()) {
                success = channels[channelIndex].mqttClient->publish(
                    config.publishTopic.c_str(),
                    config.heartbeatPackage.c_str()
                );
                if (success) {
                    Serial.printf("[通道 %d] ✓ 通过 MQTT 发送心跳包\n", channelIndex);
                } else {
                    Serial.printf("[通道 %d] ✗ 通过 MQTT 发送心跳包失败\n", channelIndex);
                }
            }
        }

        // 如果心跳发送失败，标记需要重连
        if (!success) {
            channels[channelIndex].needsReconnect = true;
            channels[channelIndex].state = ConnectionState::ERROR;
        }
    }
}

void checkConnectionStatus(int channelIndex) {
    ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[channelIndex];
    uint32_t currentTime = millis();

    // 每5秒检查一次连接状态
    if (currentTime - channels[channelIndex].lastConnectionCheck < 5000) {
        return;
    }

    channels[channelIndex].lastConnectionCheck = currentTime;

    if (!config.enabled) {
        return;
    }

    bool isConnected = false;

    if (config.protocol == "tcp") {
        isConnected = channels[channelIndex].wifiClient.connected();
    } else if (config.protocol == "mqtt") {
        isConnected = (channels[channelIndex].mqttClient != nullptr &&
                      channels[channelIndex].mqttClient->connected());
    }

    // 更新连接状态
    if (isConnected && channels[channelIndex].state != ConnectionState::CONNECTED) {
        channels[channelIndex].state = ConnectionState::CONNECTED;
        channels[channelIndex].lastHeartbeatTime = currentTime; // 重置心跳时间
        Serial.printf("[通道 %d] 连接已恢复\n", channelIndex);
    } else if (!isConnected && channels[channelIndex].state == ConnectionState::CONNECTED) {
        channels[channelIndex].state = ConnectionState::ERROR;
        channels[channelIndex].needsReconnect = true;
        Serial.printf("[通道 %d] 连接已断开\n", channelIndex);
    }
}

void handleReconnection(int channelIndex) {
    ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[channelIndex];
    uint32_t currentTime = millis();

    if (!config.enabled || !channels[channelIndex].needsReconnect) {
        return;
    }

    // 重连间隔：首次立即重连，之后每10秒重试
    if (channels[channelIndex].lastReconnectAttempt == 0 ||
        currentTime - channels[channelIndex].lastReconnectAttempt >= 10000) {

        Serial.printf("[通道 %d] 正在尝试重新连接...\n", channelIndex);

        bool success = false;
        if (config.protocol == "tcp") {
            success = connectTCP(channelIndex);
        } else if (config.protocol == "mqtt") {
            success = connectMQTT(channelIndex);
        }

        channels[channelIndex].lastReconnectAttempt = currentTime;

        if (success) {
            channels[channelIndex].needsReconnect = false;
            Serial.printf("[通道 %d] ✓ 重新连接成功\n", channelIndex);
        } else {
            Serial.printf("[通道 %d] ✗ 重新连接失败\n", channelIndex);
        }
    }
}

void handleHeartbeat(int channelIndex) {
    ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[channelIndex];
    uint32_t currentTime = millis();

    if (!config.enabled || channels[channelIndex].state != ConnectionState::CONNECTED) {
        return;
    }

    if (config.heartbeatTime > 0) {
        uint32_t heartbeatInterval = config.heartbeatTime * 1000;
        if (currentTime - channels[channelIndex].lastHeartbeatTime >= heartbeatInterval) {
            sendHeartbeat(channelIndex);
            channels[channelIndex].lastHeartbeatTime = currentTime;
        }
    }
}

void applyNetworkConfig() {
    Serial.println("[NetworkManager] 正在应用网络配置...");

    for (int i = 0; i < MAX_CHANNELS; i++) {
        ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[i];

        Serial.printf("[NetworkManager] 通道 %d - 启用: %d, 协议: %s, 目标: %s, 端口: %d, 数据源: %s\n",
                     i, config.enabled, config.protocol.c_str(), config.target.c_str(), config.port, config.source.c_str());

        if (!config.enabled) {
            Serial.printf("[NetworkManager] 通道 %d 已禁用，正在断开连接\n", i);
            disconnectChannel(i);
            continue;
        }

        // 检查配置是否有效
        if (config.target.isEmpty() || config.port == 0) {
            Serial.printf("[NetworkManager] 通道 %d 配置无效\n", i);
            channels[i].state = ConnectionState::ERROR;
            continue;
        }

        // 标记需要重新连接
        channels[i].needsReconnect = true;
        channels[i].lastReconnectAttempt = 0; // 立即尝试连接

        Serial.printf("[NetworkManager] 通道 %d 配置已更新，将重新连接\n", i);
    }
}

void loop() {
    uint32_t currentTime = millis();
    static uint32_t lastStatusPrint = 0;

    for (int i = 0; i < MAX_CHANNELS; i++) {
        ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[i];

        if (!config.enabled) {
            continue;
        }

        // 处理 MQTT 循环
        if (config.protocol == "mqtt" && channels[i].mqttClient != nullptr) {
            channels[i].mqttClient->loop();
        }

        // 检查连接状态
        checkConnectionStatus(i);

        // 处理重连
        handleReconnection(i);

        // 处理心跳
        handleHeartbeat(i);
    }

    // 每30秒打印一次状态
    if (currentTime - lastStatusPrint >= 30000) {
        printChannelStatus();
        lastStatusPrint = currentTime;
    }
}

// 优化的数据发送函数
bool sendToChannel(int channelIndex, const uint8_t* data, size_t length) {
    if (channelIndex < 0 || channelIndex >= MAX_CHANNELS) return false;

    ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[channelIndex];

    // 修正：使用正确的格式符打印协议字符串
    Serial.printf("[通道 %d] 通过 %s 发送 %d 字节\n", channelIndex, config.protocol.c_str(), length);

    if (!config.enabled || channels[channelIndex].state != ConnectionState::CONNECTED) {
        Serial.printf("[通道 %d] 不可用 - 启用: %d, 状态: %s\n",
                     channelIndex, config.enabled, getChannelStatus(channelIndex).c_str());
        return false;
    }

    bool success = false;

    if (config.protocol == "tcp") {
        size_t sent = channels[channelIndex].wifiClient.write(data, length);
        channels[channelIndex].wifiClient.flush(); // 立即发送
        success = (sent == length);
        if (success) {
            Serial.printf("[通道 %d] 通过 TCP 发送 %d 字节成功\n", channelIndex, length);
        } else {
            Serial.printf("[通道 %d] 通过 TCP 发送 %d 字节失败，仅发送 %d 字节\n",
                         channelIndex, length, sent);
            channels[channelIndex].needsReconnect = true;
        }
    } else if (config.protocol == "mqtt" && channels[channelIndex].mqttClient != nullptr) {
        success = channels[channelIndex].mqttClient->publish(config.publishTopic.c_str(), data, length);
        if (success) {
            Serial.printf("[通道 %d] 通过 MQTT 发送 %d 字节成功\n", channelIndex, length);
        } else {
            Serial.printf("[通道 %d] 通过 MQTT 发送 %d 字节失败\n", channelIndex, length);
            channels[channelIndex].needsReconnect = true;
        }
    } else {
        Serial.printf("[通道 %d] 不支持的协议: %s\n", channelIndex, config.protocol.c_str());
    }

    return success;
}

bool sendToSource(const String& source, const uint8_t* data, size_t length) {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (ConfigManager::deviceConfig.channels[i].source == source) {
            return sendToChannel(i, data, length);
        }
    }
    return false;
}

bool isChannelConnected(int channelIndex) {
    if (channelIndex < 0 || channelIndex >= MAX_CHANNELS) return false;
    return channels[channelIndex].state == ConnectionState::CONNECTED;
}

String getChannelStatus(int channelIndex) {
    if (channelIndex < 0 || channelIndex >= MAX_CHANNELS) {
        return "无效";
    }

    ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[channelIndex];
    if (!config.enabled) {
        return "已禁用";
    }

    switch (channels[channelIndex].state) {
        case ConnectionState::DISCONNECTED:
            return "已断开";
        case ConnectionState::CONNECTING:
            return "连接中";
        case ConnectionState::CONNECTED:
            return "已连接";
        case ConnectionState::ERROR:
            return "错误/重连中";
        default:
            return "未知";
    }
}

// 打印通道状态函数
static void printChannelStatus() {
    Serial.println("=== 网络通道状态 ===");
    for (int i = 0; i < MAX_CHANNELS; i++) {
        ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[i];
        String status = getChannelStatus(i);
        Serial.printf("通道 %d: %s", i, status.c_str());

        if (config.enabled) {
            Serial.printf(" (%s://%s:%d, 数据源: %s)",
                         config.protocol.c_str(), config.target.c_str(), config.port, config.source.c_str());
        }
        Serial.println();
    }
    Serial.println("====================");
}

} // namespace NetworkManager