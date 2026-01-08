#include "TimeManager.h"
#include "NetManager.h"
#include <WiFi.h>
#include <ETH.h>
#include <time.h>

using namespace NetManager;

namespace TimeManager {

static bool timeSynced = false;
static const char* ntpServer = "ntp.aliyun.com";

// 检查时间是否有效（是否早于2023年）
bool isTimeValid() {
    time_t now;
    time(&now);
    return now > 1748044800; // 2023-01-01 00:00:00
}

// 手动同步时间
void syncNow() {
    Serial.println("[TIME] 开始NTP时间同步...");

    configTime(8 * 3600, 0, ntpServer, "pool.ntp.org", "time.windows.com");

    int retry = 0;
    const int maxRetry = 15;
    time_t now = 0;

    while (retry < maxRetry) {
        time(&now);
        if (isTimeValid()) break;
        delay(1000);
        retry++;
        Serial.print(".");
    }
    Serial.println();

    if (isTimeValid()) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        Serial.printf("[TIME] 同步成功: %s (UTC+8)\n", buf);
        timeSynced = true;
    } else {
        Serial.println("[TIME] 同步失败 ⚠️");
        timeSynced = false;
    }
}

// 初始化：等待网络并同步时间
void init(int tzOffset) {
    Serial.println("[TIME] 初始化 NTP 时间同步模块...");
    setenv("TZ", "CST-8", 1);
    tzset();

    // 等待网络连接成功
    int retry = 0;
    while (retry < 20 && currentNet == NET_NONE) {
        delay(1000);
        retry++;
        Serial.print(".");
    }
    Serial.println();

    if (currentNet != NET_NONE) {
        Serial.println("[TIME] 网络已连接 ✅，准备同步时间...");
        syncNow();
    } else {
        Serial.println("[TIME] 网络连接超时 ⚠️，跳过时间同步");
    }
}

} // namespace TimeManager
