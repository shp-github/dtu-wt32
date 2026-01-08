#include "ResourceManager.h"
#include "NetManager.h"
#include "MqttManager.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ETH.h>
#include <SPIFFS.h>

using namespace NetManager;

namespace ResourceManager {

// ===============================
// 核心功能：将资源状态序列化到提供的 buffer 中
// ===============================
size_t serializeResourceStatus(char* buffer, size_t bufferSize) {
    StaticJsonDocument<700> doc;
    doc["uptime"]       = millis() / 1000;
    doc["heap_free"]    = ESP.getFreeHeap();
    doc["heap_total"]   = ESP.getHeapSize();
    doc["flash_free"]   = SPIFFS.totalBytes() - SPIFFS.usedBytes();
    doc["flash_total"]  = SPIFFS.totalBytes();
    doc["cpu_cores"]    = ESP.getChipCores();
    doc["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
    doc["chip_model"]   = ESP.getChipModel();
    doc["flash_size"]   = ESP.getFlashChipSize();
    if (currentNet == NET_ETH) {
        doc["network"] = "ETH";
        doc["ip"]      = ETH.localIP().toString();
    } else {
        doc["network"] = "NONE";
        doc["ip"]      = "0.0.0.0";
    }
    return serializeJson(doc, buffer, bufferSize);
}

// ===============================
// 功能1：打印资源状态到串口
// ===============================
void printResourceStatus() {
    char payloadBuffer[512];
    serializeResourceStatus(payloadBuffer, sizeof(payloadBuffer));
    Serial.printf("[RESOURCE_PRINT] %s\n", payloadBuffer);
}

// ===============================
// 功能2：上传资源状态到MQTT
// ===============================
void uploadResourceStatus() {
    char payloadBuffer[512];
    serializeResourceStatus(payloadBuffer, sizeof(payloadBuffer));
    Serial.printf("[RESOURCE_UPLOAD] 上传硬件信息: %s\n", payloadBuffer);
    MqttManager::publish(("/dev/status/" + clientId).c_str(), payloadBuffer);
}

// ===============================
// 后台任务：周期性打印和上传资源
// ===============================
void taskResource(void* pvParameters)
{
    const unsigned long uploadInterval = 300000; // 上传间隔：5分钟
    const unsigned long printInterval = 5000;    // 打印间隔：5秒
    unsigned long lastUploadTime = 0;
    unsigned long lastPrintTime = 0;
    while (true)
    {
        unsigned long now = millis();
        // 检查是否到了打印时间
        if (now - lastPrintTime >= printInterval) {
            printResourceStatus();
            lastPrintTime = now;
        }
        // 检查是否到了上传时间
        if (now - lastUploadTime >= uploadInterval) {
            uploadResourceStatus();
            lastUploadTime = now;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS); // 每秒检查一次
    }
}


// ===============================
// 初始化
// ===============================
void init()
{
    SPIFFS.begin(true);
    // 启动资源监控任务
    xTaskCreatePinnedToCore(
        taskResource,
        "ResourceTask",
        3072,
        NULL,
        1, // 优先级
        NULL,
        0  // 运行在核心 0
    );
    Serial.println("[RESOURCE] ResourceManager 初始化完成（支持5秒打印）");
}

} // namespace ResourceManager