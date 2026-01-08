#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <ETH.h>
#include "ConfigManager.h"
#include "OtaManager.h"

namespace DiscoveryManager {

    struct MqttConfig {
        String server;
        int port = 1883;
        String username = "device";
        String password = "123456";
    };

// ===== 初始化 Discovery 模块 =====
// client: 设备唯一 ID（通常用 MAC）
// 创建广播任务和单播接收任务
void init(const String &client);

void publishOtaStatus(const char* status, const char* msg = nullptr);

bool connectMqtt(const MqttConfig& cfg);

} // namespace DiscoveryManager
