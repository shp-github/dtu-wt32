#include "MqttManager.h"
#include "NetManager.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ETH.h>
#include "ModbusManager.h"
#include "AutomationManager.h"
#include "OtaManager.h"
#include "ConfigManager.h"

using namespace NetManager;


// 设置定时
void handleTimeMessage(const String& msg);

// ===== MQTT 客户端 =====
WiFiClient netClient;
PubSubClient mqttClient(netClient);

// mqtt连接信息
const char* mqtt_server = "121.36.223.224";
const int mqtt_port = 1883;
const char* mqtt_username = "admin";
const char* mqtt_password = "Abc123456aifamily!";

// ===== MQTT 回调 =====
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  String t = String(topic);

  Serial.printf("[MQTT] 收到消息 [%s]: %s\n", t.c_str(), msg.c_str());

  // 下发开关控制
  if (t.startsWith("/server/coo/")) {
    ModbusManager::handleMqttMessage(msg);
  }
  // 自动化+定时统一处理
  else if (t.startsWith("/server/auto/")) {
    AutomationManager::handleMqttMessage(msg);
  }
  // 远程指令
  else if (t.startsWith("/server/cmd/")) {
    // TODO: 远程指令处理模块
    Serial.println("[MQTT] 收到远程指令消息");

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, msg);

    if (error) {
      Serial.println("[MQTT] 远程指令解析失败");
      return;
    }

    // 检查指令的 flag 是否为 "update"
    if (doc.containsKey("flag") && doc["flag"] == "update") {
      String firmware_url = doc["firmware_url"];

      // 调用 OTA 更新方法，这里可以先进行 CRC32 校验
      Serial.printf("[MQTT] 收到远程更新指令, 固件 URL: %s\n", firmware_url.c_str());

      OtaManager::startOTAUpdate(firmware_url.c_str());
    } 
    // 其他
    else {
      Serial.println("[MQTT] 无效的远程指令");
    }

  }
  // 设置时间
  else if (t.startsWith("/server/time/")) {
    handleTimeMessage(msg);
  } else {
    Serial.println("[MQTT] 未知主题消息");
  }
}



// ===== MQTT 连接 =====
void reconnect() {
  if (!mqttClient.connected() && currentNet != NET_NONE) {

    Serial.print("[MQTT] 尝试连接, ClientID: ");
    Serial.println(clientId);
    if (mqttClient.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("[MQTT] 连接成功");

      // 订阅各类控制和规则主题
      mqttClient.subscribe(("/server/coo/" + clientId).c_str());   // 开关控制
      mqttClient.subscribe(("/server/auto/" + clientId).c_str());  // 自动化/定时规则
      mqttClient.subscribe(("/server/cmd/" + clientId).c_str());   // 远程指令
      mqttClient.subscribe(("/server/time/" + clientId).c_str());  // 更新时间
      Serial.println("[MQTT] 订阅完成");
    } else {
      Serial.printf("[MQTT] 连接失败 rc=%d\n", mqttClient.state());
    }
  }
}

// ===== MQTT 主循环任务 =====
void MqttManager::taskMQTT(void* pvParameters) {
  Serial.println("[MQTT] MQTT 任务启动");
  static unsigned long lastReconnect = 0;
  static unsigned long lastHeartbeat = 0;

  while (true) {
    if (currentNet != NET_NONE) {
      if (!mqttClient.connected() && millis() - lastReconnect > 5000) {
        reconnect();
        lastReconnect = millis();
      }
      mqttClient.loop();

     // 发送心跳信息（动态读取 ConfigManager 设置的心跳间隔）
    int interval = ConfigManager::getConfig().heart_interval;
    if (mqttClient.connected() && millis() - lastHeartbeat >= interval * 1000) {
        mqttClient.publish(("/dev/coo/" + clientId).c_str(), clientId.c_str());
        Serial.printf("[MQTT] 心跳发送: %s (间隔 %d 秒)\n", clientId.c_str(), interval);
        lastHeartbeat = millis();  // 更新心跳时间
    }

    } else {
      Serial.println("[MQTT] 网络未就绪, 等待...");
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// ===== 发布消息接口 =====
void MqttManager::publish(const String& topic, const String& payload) {
  if (mqttClient.connected()) {
    mqttClient.publish(topic.c_str(), payload.c_str());
    Serial.printf("[MQTT] 发布消息: [%s] %s\n", topic.c_str(), payload.c_str());
  } else {
    Serial.println("[MQTT] 发布失败, MQTT 未连接");
  }
}

// ===== 初始化 =====
void MqttManager::init() {
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(callback);
  mqttClient.setBufferSize(4096);
  Serial.println("[MQTT] MQTT 初始化完成");
}

// ===== 启动 FreeRTOS 任务 =====
void MqttManager::startTasks() {
  xTaskCreatePinnedToCore(taskMQTT, "MQTTTask", 10000, NULL, 2, NULL, 1);
}

void initRTC() {
  setenv("TZ", "CST-8", 1);  // 设置时区
  tzset();

  // 默认初始化时间，避免第一次 taskAutomation 时间为 1970
  struct timeval now = { 1698192000, 0 };  // 示例：2023-10-25 00:00:00
  settimeofday(&now, NULL);
}

void handleTimeMessage(const String& msg) {
  time_t ts = 0;

  // 尝试解析 JSON 格式的时间戳
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, msg);
  if (!err && doc.containsKey("timestamp")) {
    ts = (time_t)doc["timestamp"].as<long long>();
  } else {
    // 如果解析失败，尝试将 msg 当作纯数字解析
    bool allDigits = true;
    int start = 0;
    if (msg.length() > 0 && (msg[0] == '+' || msg[0] == '-')) start = 1;
    for (int i = start; i < (int)msg.length(); ++i) {
      if (!isDigit(msg[i])) {
        allDigits = false;
        break;
      }
    }
    if (allDigits) {
      ts = (time_t)atoll(msg.c_str());
    } else {
      Serial.println("[TIME] 时间消息解析失败（不是 JSON 也不是数字）");
      return;
    }
  }

  if (ts <= 1000000000) {
    // 很小的时间戳（可视为异常），打印警告但仍设置
    Serial.printf("[TIME] 收到可疑时间戳: %ld\n", (long)ts);
  }

  struct timeval now;
  now.tv_sec = ts;
  now.tv_usec = 0;

  // 设置系统时间
  if (settimeofday(&now, NULL) == 0) {
    // 将时间调整为北京时间（UTC + 8小时）
    struct tm* tm_info = localtime(&now.tv_sec);
    tm_info->tm_hour += 8;  // 增加8小时，转为北京时间

    // 如果时间溢出，处理时钟
    if (tm_info->tm_hour >= 24) {
      tm_info->tm_hour -= 24;
      // 如果小时变动了，可能影响到日期（天、月、年等），需要调用 mktime 调整日期
      mktime(tm_info);
    }

    // 更新系统时间
    now.tv_sec = mktime(tm_info);  // 获取更新后的时间戳

    if (settimeofday(&now, NULL) == 0) {
      // 打印人类可读时间
      time_t t = now.tv_sec;
      struct tm* tm_now = localtime(&t);
      char buf[64];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_now);
      Serial.printf("[TIME] 系统时间更新成功（北京时间）: %s (ts=%ld)\n", buf, (long)now.tv_sec);

      // ==== 时间设置后触发规则检查 ====
      Serial.println("[TIME] 时间同步成功，触发一次自动化规则检查");
      // 调用单次检查规则
      AutomationManager::triggerOnce();  // 触发规则检查
    } else {
      Serial.println("[TIME] settimeofday 失败");
    }
  } else {
    Serial.println("[TIME] settimeofday 失败");
  }
}
