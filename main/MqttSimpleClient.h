#pragma once
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "OtaManager.h"

class MqttSimpleClient {
public:
    // 获取单例实例
    static MqttSimpleClient& getInstance();

    // 设置MQTT服务器信息
    void setServer(const String& server, int port,
                  const String& username = "",
                  const String& password = "");

    // 初始化MQTT
    void init();

    // 启动MQTT任务
    void start();

    // 发布消息
    void publish(const String& topic, const String& payload);

    // 立即发送消息（如果未连接会尝试立即连接）
    bool sendImmediate(const String& topic, const String& payload);

    // 检查连接状态
    bool isConnected();

    // 手动重连
    void reconnect();

    // 设置消息回调函数
    void setMessageCallback(void (*callback)(const String& topic, const String& message));

    // 处理 server/cmd
    void handleCmd(const String& message);

    // 处理 server/coo
    void handleCoo(const String& message);

private:
    MqttSimpleClient();

    WiFiClient netClient;
    PubSubClient mqttClient;

    String mqttServer;
    int mqttPort = 1883;
    String mqttUsername;
    String mqttPassword;

    // 消息回调函数指针
    void (*messageCallback)(const String& topic, const String& message) = nullptr;

    // 内部方法
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    static void taskMQTT(void* pvParameters);

    // 检查服务器连通性
    bool checkServerReachable(const String& server, int port);
};