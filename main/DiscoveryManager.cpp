#include "DiscoveryManager.h"
#include "MqttSimpleClient.h"

namespace DiscoveryManager {

/* ============================ */
/*       🔧 可调参数区（顶部）   */
/* ============================ */

/* 广播间隔（毫秒） */
static const uint32_t DISCOVERY_INTERVAL_MS = 5000;

/* UDP 端口 */
static const int DISCOVERY_PORT = 4210;
static const int CONFIG_PORT    = 4211;

/* 固件版本 */
static const char* FIRMWARE_VERSION = "v1.0.7";

/* 是否打印日志 */
static const bool ENABLE_LOG = true;

/* ============================ */
/*   内部静态变量（状态信息）    */
/* ============================ */

static String deviceId;
static TaskHandle_t discoveryTaskHandle = nullptr;
static TaskHandle_t configTaskHandle    = nullptr;

/* ============================ */
/*       🔍 工具函数区域         */
/* ============================ */

// 获取 MAC 地址
static String getMac() {
    if (ETH.linkUp()) return ETH.macAddress();
    return WiFi.macAddress();
}

// 获取网络类型
static String getNetworkType() {
    if (ETH.linkUp()) return "ETH";
    if (WiFi.status() == WL_CONNECTED) return "WiFi";
    return "NONE";
}

// 获取当前 IP
static IPAddress getCurrentIP() {
    if (ETH.linkUp()) return ETH.localIP();
    return WiFi.localIP();
}

// 获取 WiFi 信号强度
static int getRSSI() {
    if (ETH.linkUp()) return 0;
    if (WiFi.status() == WL_CONNECTED) return WiFi.RSSI();
    return 0;
}

// 获取运行时间（秒）
static uint32_t getRuntime() {
    return millis() / 1000;
}

/* ============================ */
/*        📢 广播核心函数        */
/* ============================ */

static void sendDiscoveryPacket(WiFiUDP &udp) {
    IPAddress ip = getCurrentIP();
    if (!ip || ip.toString() == "0.0.0.0") {
        if (ENABLE_LOG) Serial.println("[DISCOVERY] 网络未就绪，跳过广播");
        return;
    }

    StaticJsonDocument<256> doc;
    doc["name"] = ConfigManager::getConfig().name;
    doc["type"] = "discover";
    doc["id"] = deviceId;
    doc["mac"] = getMac();
    doc["ip"] = ip.toString();
    doc["networkType"] = getNetworkType();
    doc["RSSI"] = getRSSI();
    doc["runtime"] = getRuntime();
    doc["firmware"] = FIRMWARE_VERSION;
    doc["heart_interval"] = ConfigManager::getConfig().heart_interval;

    char buf[256];
    size_t len = serializeJson(doc, buf, sizeof(buf));

    udp.beginPacket(IPAddress(255,255,255,255), DISCOVERY_PORT);
    udp.write((uint8_t*)buf, len);
    udp.endPacket();

    if (ENABLE_LOG) Serial.printf("[DISCOVERY] 广播数据: %s\n", buf);
}

/* ============================ */
/*       📡 广播任务            */
/* ============================ */

static void taskDiscovery(void *pvParameters) {
    WiFiUDP udp;

    Serial.println("[DISCOVERY] 广播任务等待网络连接...");

    // 等待网络就绪
    while (!(ETH.linkUp() || WiFi.status() == WL_CONNECTED)) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    udp.begin(DISCOVERY_PORT);
    Serial.printf("[DISCOVERY] UDP 广播端口 %d 已初始化\n", DISCOVERY_PORT);

    for (;;) {
        sendDiscoveryPacket(udp);
        vTaskDelay(DISCOVERY_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

/* ============================ */
/*      ⚙️ 配置监听任务         */
/* ============================ */

static void taskConfig(void *pvParameters) {
    WiFiUDP udp;

    Serial.println("[CONFIG] 配置监听任务等待网络连接...");

    // 等待网络就绪
    while (!(ETH.linkUp() || WiFi.status() == WL_CONNECTED)) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    udp.begin(CONFIG_PORT);
    Serial.printf("[CONFIG] UDP 监听端口 %d 已初始化\n", CONFIG_PORT);

    char buf[1024];

    for (;;) {
        int packetSize = udp.parsePacket();
        if (packetSize > 0) {
            int len = udp.read(buf, sizeof(buf)-1);
            buf[len] = 0;

            IPAddress senderIP = udp.remoteIP();
            uint16_t senderPort = udp.remotePort();
            String ip = senderIP.toString();

            if (ENABLE_LOG) {
                Serial.printf("[CONFIG] 收到 UDP 包, size=%d, 来自 %s:%d\n", packetSize, ip, senderPort);
                Serial.printf("[CONFIG] 包内容: %s\n", buf);
            }

            StaticJsonDocument<1024> doc;


            if (deserializeJson(doc, buf) == DeserializationError::Ok) {
                String type = doc["type"] | "";


                //远程升级 --------------------------------------------
                if (type == "upgrade") {
                    Serial.println("[CONFIG] 处理 'upgrade' 消息...");

                    // 提取升级信息
                    String fileName = doc["fileName"] | "";
                    String downloadUrl = doc["downloadUrl"] | "";

                    DiscoveryManager::MqttConfig cfg;
                    cfg.server   = doc["ip"] | ip;
                    cfg.port     = doc["mqttPort"] | 1883;
                    cfg.username = doc["mqttUsername"] | "device";
                    cfg.password = doc["mqttPassword"] | "123456";
                    DiscoveryManager::connectMqtt(cfg);

                    // 如果提供了下载地址，启动 OTA 更新
                    if (downloadUrl.length() > 0) {
                        Serial.printf("[UPGRADE] 开始 OTA 更新: %s\n", downloadUrl.c_str());
                        // 调用 OTA 更新
                        OtaManager::startOTAUpdate(downloadUrl.c_str());
                    } else {
                        Serial.println("[UPGRADE] 错误: 未提供下载地址");
                    }
                }

                //连接mqtt服务 --------------------------------------------
                else if (type == "connect-mqtt") {
                    Serial.println("[CONFIG] 处理 'connect-mqtt' 消息...");

                    DiscoveryManager::MqttConfig cfg;
                    cfg.server   = doc["ip"] | ip;
                    cfg.port     = doc["mqttPort"] | 1883;
                    cfg.username = doc["mqttUsername"] | "device";
                    cfg.password = doc["mqttPassword"] | "123456";
                    DiscoveryManager::connectMqtt(cfg);

                }

                //重启设备 --------------------------------------------
                else if (type == "reboot") {
                    Serial.println("[CMD] 收到指令: 重启设备");
                    xTaskCreate([](void*){delay(100);ESP.restart();}, "RebootTask", 1024, NULL, 999, NULL);
                    return;
                }

                //未知消息类型 --------------------------------------------
                else {
                    if (ENABLE_LOG) Serial.printf("[CONFIG] 未知消息类型: %s\n", type.c_str());
                }

            } else {
                if (ENABLE_LOG) Serial.println("[CONFIG] JSON 解析失败");
            }
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}


bool connectMqtt(const MqttConfig& cfg) {

    if (cfg.server.length() == 0) {
        if (ENABLE_LOG) Serial.println("[MQTT] MQTT服务器地址为空，无法连接");
        return false;
    }

    if (ENABLE_LOG) {
        Serial.printf("[MQTT] 尝试连接 MQTT: %s:%d 用户:%s\n",
                      cfg.server.c_str(), cfg.port, cfg.username.c_str());
    }

    // 设置服务器
    MqttSimpleClient::getInstance().setServer(cfg.server, cfg.port, cfg.username, cfg.password);

    // 尝试重连
    MqttSimpleClient::getInstance().reconnect();

    // 检查连接状态
    bool connected = MqttSimpleClient::getInstance().isConnected();
    if (ENABLE_LOG) {
        if (connected) {
            Serial.println("[MQTT] 连接成功，数据已发送");
        } else {
            Serial.println("[MQTT] 连接失败，数据发送可能失败");
        }
    }

    return connected;
}


/* ============================ */
/*        🚀 初始化函数         */
/* ============================ */

void init(const String &client) {
    deviceId = client;
    ConfigManager::begin();

    Serial.printf("[DISCOVERY] 初始化 DiscoveryManager, deviceId=%s\n", deviceId.c_str());

    if (!discoveryTaskHandle) {
        xTaskCreatePinnedToCore(taskDiscovery, "DiscoveryTask", 4096, nullptr, 1, &discoveryTaskHandle, 0);
        if (ENABLE_LOG)
            Serial.println("[DISCOVERY] 广播任务创建成功");
    }

    if (!configTaskHandle) {
        xTaskCreatePinnedToCore(taskConfig, "ConfigTask", 10000, nullptr, 1, &configTaskHandle, 0);
        if (ENABLE_LOG)
            Serial.println("[CONFIG] 接收任务创建成功");
    }

    //启动mqtt客户端
    Serial.println("初始化 MQTT 客户端...");
    MqttSimpleClient::getInstance().init();
    MqttSimpleClient::getInstance().start();

}

} // namespace DiscoveryManager
