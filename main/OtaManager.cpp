#include "OtaManager.h"
#include "NetManager.h"
#include "MqttManager.h"
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "MqttSimpleClient.h"
#include "esp_ota_ops.h"

using namespace NetManager;



// 检查更新间隔
const int check_interval = 180;  

// ================== 通用状态上报 ==================
void publishOtaStatus(const char* status, const char* msg = nullptr, int progress = -1) {
    StaticJsonDocument<256> doc;
    doc["status"] = status;
    if (msg) doc["message"] = msg;
    if (progress >= 0) doc["progress"] = progress;  // ✅ 实时百分比

    String payload;
    serializeJson(doc, payload);


    Serial.printf("[OTA] 推送状态: %s\n", payload.c_str());

    //发送云端服务器
    MqttManager::publish("/dev/ota/" + String(clientId), payload);

    //发送局域网配置软件
    MqttSimpleClient::getInstance().sendImmediate("/dev/ota/" + String(clientId), payload);


}


bool OtaManager::downloadAndUpdateFirmware(const char* firmware_url) {

    HTTPClient http;
    http.begin(firmware_url);
    http.setReuse(false);

    int code = http.GET();
    Serial.printf("[OTA] HTTP 返回码: %d\n", code);

    if (code != 200) {
        publishOtaStatus("failed", "固件下载失败");
        return false;
    }

    int len = http.getSize();   // 必须先 GET 后才能得到真实大小
    Serial.printf("[OTA] 固件大小: %d bytes\n", len);

    if (len <= 0) {
        publishOtaStatus("failed", "固件大小无效");
        return false;
    }

    // ================== OTA 分区大小检查 ==================
    const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
    Serial.printf("[OTA] OTA Partition Size: %d bytes\n", partition->size);

    if (partition->size < len) {
        publishOtaStatus("failed", "固件大于OTA分区，无法更新");
        http.end();
        return false;
    }

    // ================== 开始更新 ==================
    if (!Update.begin(len)) {
        publishOtaStatus("failed", "Update.begin 失败");
        http.end();
        return false;
    }

    publishOtaStatus("downloading", "开始下载固件...");

    WiFiClient* stream = http.getStreamPtr();
    const size_t buffSize = 4096;
    uint8_t buff[buffSize];
    size_t written = 0;

    unsigned long lastUpdate = millis();

    while (written < len && http.connected()) {
        size_t avail = stream->available();
        if (avail) {
            int readBytes = stream->readBytes(buff, min(avail, buffSize));
            if (readBytes > 0) {
                Update.write(buff, readBytes);
                written += readBytes;

                int progress = (written * 100) / len;
                if (millis() - lastUpdate > 1000) {
                    char msg[64];
                    sprintf(msg, "下载中 %d%% (%d/%d KB)",
                        progress, written / 1024, len / 1024);
                    publishOtaStatus("downloading", msg, progress);
                    lastUpdate = millis();
                }
            }
        } else {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    publishOtaStatus("writing", "固件写入完成，正在校验...", 100);

    if (!Update.end() || !Update.isFinished()) {
        publishOtaStatus("failed", "Update.end 或 isFinished 失败");
        Serial.printf("[OTA] 更新失败：%s\n", Update.errorString());
        http.end();
        return false;
    }

    publishOtaStatus("success", "升级完成，准备重启", 100);
    delay(1500);
    ESP.restart();
    return true;
}

// ================== 检查并更新固件 ==================
void OtaManager::checkAndUpdateFirmware() {

    // 1️⃣ 使用 String 拼接
    String url = String("http://121.36.223.224/home/file/ota/") + clientId + ".bin";

    // 2️⃣ 在 HTTPClient 需要 const char* 时使用 c_str()
    const char* firmware_url = url.c_str();

    Serial.println("[OTA] 开始检查更新...");
    publishOtaStatus("checking", "开始检查更新");

    HTTPClient http;
    http.begin(firmware_url);
    int code = http.GET();
    Serial.printf("[OTA] HTTP 返回码: %d\n", code);

    if (code == 200) {
        int len = http.getSize();
        Serial.printf("[OTA] 固件大小: %d bytes (%.2f KB)\n", len, len / 1024.0);

        if (len > 0) {
            if (!downloadAndUpdateFirmware(firmware_url)) {
                Serial.println("[OTA] 固件下载更新失败");
            }
        } else {
            publishOtaStatus("failed", "固件大小无效");
            Serial.println("[OTA] 固件大小无效");
        }
    } else {
        publishOtaStatus("none", "无可用更新");
        Serial.println("[OTA] 无可用更新");
    }

    http.end();
}

// ================== OTA 任务 ==================
void OtaManager::taskOTA(void* pvParameters) {
    while (true) {
        if (currentNet != NET_NONE) {
            checkAndUpdateFirmware();  // 统一调用封装好的检查更新函数
        }
        vTaskDelay(check_interval * 1000 / portTICK_PERIOD_MS);
    }
}

// ================== 启动 OTA 更新 ==================
void OtaManager::startOTAUpdate(const char* firmware_url) {
    Serial.println("[OTA] 手动触发 OTA 更新...");
    if (!downloadAndUpdateFirmware(firmware_url)) {
        Serial.println("[OTA] OTA 更新失败");
    }
}

// ================== 初始化入口 ==================
void OtaManager::init() {
    xTaskCreatePinnedToCore(taskOTA, "OTATask", 8192, NULL, 1, NULL, 1);
}
