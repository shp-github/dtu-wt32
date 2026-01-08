#include "MqttSimpleClient.h"
#include "NetManager.h"
#include "ConfigManager.h"

MqttSimpleClient::MqttSimpleClient() : mqttClient(netClient) {}

MqttSimpleClient& MqttSimpleClient::getInstance() {
    static MqttSimpleClient instance;
    return instance;
}

bool MqttSimpleClient::checkServerReachable(const String& server, int port) {
    Serial.printf("[MQTTSimpleClient] 检查服务器连通性: %s:%d\n", server.c_str(), port);

    WiFiClient testClient;
    if (testClient.connect(server.c_str(), port)) {
        Serial.println("[MQTTSimpleClient] 服务器可达");
        testClient.stop();
        return true;
    } else {
        Serial.println("[MQTTSimpleClient] 服务器不可达");
        return false;
    }
}

void MqttSimpleClient::setServer(const String& server, int port,
                           const String& username,
                           const String& password) {
    mqttServer = server;
    mqttPort = port;
    mqttUsername = username;
    mqttPassword = password;

    Serial.printf("[MQTTSimpleClient] 设置服务器: %s:%d\n", server.c_str(), port);
    mqttClient.setServer(mqttServer.c_str(), mqttPort);
}

void MqttSimpleClient::setMessageCallback(void (*callback)(const String& topic, const String& message)) {
    this->messageCallback = callback;
}

void MqttSimpleClient::init() {
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(4096);
    Serial.println("[MQTTSimpleClient] 初始化完成");
}

void MqttSimpleClient::start() {
    // 启动MQTT任务
    xTaskCreatePinnedToCore(taskMQTT, "MQTTTask", 8192, NULL, 1, NULL, 0);
    Serial.println("[MQTTSimpleClient] 任务启动");
}

void MqttSimpleClient::mqttCallback(char* topic, byte* payload, unsigned int length) {

    Serial.printf("[MQTTSimpleClient DEBUG] 收到原始数据 - 主题指针: %p, 载荷指针: %p, 长度: %u\n",
                      topic, payload, length);

    String topicStr = String(topic);
    String message = String((char*)payload, length);

    Serial.printf("[MQTTSimpleClient] 收到消息 [%s]: %s\n", topicStr.c_str(), message.c_str());

    auto& inst = MqttSimpleClient::getInstance();

    String cmdTopic = "/server/cmd/" + NetManager::clientId;
    String cooTopic = "/server/coo/" + NetManager::clientId;

    if (topicStr == cmdTopic) {
        inst.handleCmd(message);
        return;  // 避免后续进入外部回调
    }

    if (topicStr == cooTopic) {
        inst.handleCoo(message);
        return;
    }

    // 其它主题给用户自定义回调
    if (inst.messageCallback) {
        inst.messageCallback(topicStr, message);
    }

}


// ===============================
// 处理 /server/cmd/<deviceId>
// ===============================
void MqttSimpleClient::handleCmd(const String& message) {
    Serial.println("[MQTTSimpleClient] 处理 CMD 消息: " + message);

    StaticJsonDocument<3000> doc;
    DeserializationError err = deserializeJson(doc, message);
    if (err) {
        Serial.printf("[CMD] JSON 解析失败: %s\n", err.c_str());
        return;
    }

    String type = doc["type"] | "";
    String flag = doc["flag"] | "";  // 可以指定模块 interface / network / channels

    if (type == "get_client_id") {
        Serial.println("[CMD] 收到指令: 获取设备号" + flag);
        publish("/dev/cmd/" + NetManager::clientId, NetManager::clientId);
        return;
    }

    if (type == "get_config") {
        Serial.println("[CMD] 收到指令: 读取配置" + flag);

        StaticJsonDocument<3000> outDoc;
        JsonObject cfg = ConfigManager::serializeModule(outDoc, flag);
        String payload;
        serializeJson(cfg, payload);

        publish("/dev/cmd/" + NetManager::clientId, payload);
        return;
    }

    if (type == "set_config") {
        Serial.println("[CMD] 收到指令: 设置配置"+ flag);

        if (flag.length() == 0) {
            Serial.println("[CMD] 未指定 flag，无法设置模块");
            publish("/dev/cmd/" + NetManager::clientId, "{\"status\":\"fail\",\"reason\":\"no_flag\"}");
            return;
        }

        // 更新对应模块
        ConfigManager::updateModuleFromJson(doc);

        // 回复成功
        StaticJsonDocument<256> ack;
        ack["status"] = "ok";
        ack["type"] = "set_config";
        ack["flag"] = flag;
        String ackStr;
        serializeJson(ack, ackStr);
        publish("/dev/cmd/" + NetManager::clientId, ackStr);

        return;
    }


    if (type == "reboot") {
        Serial.println("[CMD] 收到指令: 重启设备");
        publish("/dev/cmd/" + NetManager::clientId,"{\"status\":\"ok\",\"type\":\"reboot\"}");

        // 延迟 100ms 保证消息发送出去，再重启
        xTaskCreate([](void*){
            delay(100);
            ESP.restart();
        }, "RebootTask", 1024, NULL, 1, NULL);
        return;
    }

    if (type == "ota") {
        Serial.println("[CMD] 收到指令: 设备升级");
        String downloadUrl = doc["downloadUrl"] | "";
        OtaManager::startOTAUpdate(downloadUrl.c_str());
        return;
    }

    // 未知命令
    Serial.println("[CMD] 未知的 type: " + type);
}



// ===============================
// 处理 /server/coo/<deviceId>
// ===============================
void MqttSimpleClient::handleCoo(const String& message) {
    Serial.println("[MQTTSimpleClient] 处理 COO 消息: " + message);
}



void MqttSimpleClient::taskMQTT(void* pvParameters) {
    Serial.println("[MQTTSimpleClient] MQTT任务运行中");
    MqttSimpleClient& mqtt = MqttSimpleClient::getInstance();
    unsigned long lastReconnect = 0;
    unsigned long lastHeartbeat = 0;
    unsigned long lastStatusPrint = 0;

    while (true) {
        // 每10秒打印一次状态（用于调试）
        if (millis() - lastStatusPrint > 10000) {
            Serial.printf("[MQTTSimpleClient] 状态: 连接=%s, 服务器=%s:%d, 客户端ID=%s\n",
                         mqtt.mqttClient.connected() ? "是" : "否",
                         mqtt.mqttServer.c_str(), mqtt.mqttPort,
                         NetManager::clientId.c_str());
            lastStatusPrint = millis();
        }

        // 重连逻辑
        if (!mqtt.mqttClient.connected()) {
            if (millis() - lastReconnect > 5000) {
                Serial.println("[MQTTSimpleClient] 尝试连接...");
                Serial.printf("[MQTTSimpleClient] 连接参数: clientId=%s, username=%s\n",
                             NetManager::clientId.c_str(), mqtt.mqttUsername.c_str());

                // 检查服务器连通性
                if (mqtt.checkServerReachable(mqtt.mqttServer, mqtt.mqttPort)) {
                    bool connected = mqtt.mqttClient.connect(NetManager::clientId.c_str(),
                                                           mqtt.mqttUsername.c_str(),
                                                           mqtt.mqttPassword.c_str());

                    if (connected) {
                        Serial.println("[MQTTSimpleClient] 连接成功");

                        // 订阅主题
                        bool sub1 = mqtt.mqttClient.subscribe(("/server/coo/" + NetManager::clientId).c_str());
                        bool sub2 = mqtt.mqttClient.subscribe(("/server/cmd/" + NetManager::clientId).c_str());

                        Serial.printf("[MQTTSimpleClient] 订阅结果: 控制=%s, 命令=%s\n",
                                     sub1 ? "成功" : "失败", sub2 ? "成功" : "失败");

                        // 立即发送一个连接成功消息
                        mqtt.publish("/dev/coo/" + NetManager::clientId,  NetManager::clientId);

                    } else {
                        int state = mqtt.mqttClient.state();
                        Serial.printf("[MQTTSimpleClient] 连接失败, 状态码=%d\n", state);
                    }
                } else {
                    Serial.println("[MQTTSimpleClient] 服务器不可达，跳过连接尝试");
                }
                lastReconnect = millis();
            }
        }

        // MQTT循环处理
        if (mqtt.mqttClient.connected()) {
            mqtt.mqttClient.loop();

            // 心跳（每30秒）
            if (millis() - lastHeartbeat > 30000) {
                mqtt.publish("/dev/coo/" + NetManager::clientId, NetManager::clientId);
                lastHeartbeat = millis();
            }
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void MqttSimpleClient::publish(const String& topic, const String& payload) {
    if (mqttClient.connected()) {
        mqttClient.publish(topic.c_str(), payload.c_str());
        Serial.printf("[MQTTSimpleClient] 发布: [%s] %s\n", topic.c_str(), payload.c_str());
    } else {
        Serial.println("[MQTTSimpleClient] 发布失败 - 未连接");
    }
}

bool MqttSimpleClient::sendImmediate(const String& topic, const String& payload) {
    Serial.printf("[MQTTSimpleClient] 立即发送: [%s] %s\n", topic.c_str(), payload.c_str());

    // 如果未连接，立即尝试连接
    if (!mqttClient.connected()) {
        Serial.println("[MQTTSimpleClient] 未连接，立即尝试连接...");

        // 检查服务器连通性
        if (!checkServerReachable(mqttServer, mqttPort)) {
            Serial.println("[MQTTSimpleClient] 服务器不可达，立即发送失败");
            return false;
        }

        bool connected = mqttClient.connect(NetManager::clientId.c_str(), mqttUsername.c_str(), mqttPassword.c_str());

        if (connected) {
            Serial.println("[MQTTSimpleClient] 立即连接成功");
            // 立即订阅
            mqttClient.subscribe(("/server/coo/" + NetManager::clientId).c_str());
            mqttClient.subscribe(("/server/cmd/" + NetManager::clientId).c_str());
        } else {
            Serial.printf("[MQTTSimpleClient] 立即连接失败, rc=%d\n", mqttClient.state());
            return false;
        }
    }

    // 发送消息
    bool result = mqttClient.publish(topic.c_str(), payload.c_str());
    if (result) {
        Serial.println("[MQTTSimpleClient] 立即发送成功");
    } else {
        Serial.println("[MQTTSimpleClient] 立即发送失败");
    }

    return result;
}

bool MqttSimpleClient::isConnected() {
    return mqttClient.connected();
}

void MqttSimpleClient::reconnect() {
    Serial.println("[MQTTSimpleClient] 强制重连...");

    // 断开现有连接
    if (mqttClient.connected()) {
        mqttClient.disconnect();
        Serial.println("[MQTTSimpleClient] 已断开现有连接");
    }

    // 立即尝试连接（不等待任务循环）
    Serial.println("[MQTTSimpleClient] 立即尝试连接...");

    // 检查服务器连通性
    if (!checkServerReachable(mqttServer, mqttPort)) {
        Serial.println("[MQTTSimpleClient] 服务器不可达，跳过立即连接");
        return;
    }

    bool connected = mqttClient.connect(NetManager::clientId.c_str(), mqttUsername.c_str(), mqttPassword.c_str());

    if (connected) {
        Serial.println("[MQTTSimpleClient] 立即连接成功");

        // 立即订阅
        mqttClient.subscribe(("/server/coo/" + NetManager::clientId).c_str());
        mqttClient.subscribe(("/server/cmd/" + NetManager::clientId).c_str());
        Serial.println("[MQTTSimpleClient] 立即订阅完成");
        Serial.println(("/server/coo/" + NetManager::clientId).c_str());
        Serial.println(("/server/cmd/" + NetManager::clientId).c_str());

        // 立即发送测试消息
        publish("/dev/coo/" + NetManager::clientId, NetManager::clientId);

    } else {
        Serial.printf("[MQTTSimpleClient] 立即连接失败, rc=%d\n", mqttClient.state());
    }
}