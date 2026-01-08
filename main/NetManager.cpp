#include "NetManager.h"
#include <WiFi.h>
#include <ETH.h>
#include <Preferences.h>
#include <ESPmDNS.h>

namespace NetManager {

    NetType currentNet = NET_NONE;
    String clientId;

    #define PHY_RST_PIN 5

    static bool ethInitialized = false;
    static bool ethLinkUp = false;

    // ========== 新增：静态IP相关变量 ==========
    static NetMode currentMode = NET_MODE_DHCP;  // 默认为DHCP
    static IPAddress staticIP;
    static IPAddress staticGateway;
    static IPAddress staticSubnet;
    static IPAddress staticDNS;

    // ===== 获取以太网 MAC（去掉冒号） =====
    String getEthMacNoColon() {
        uint8_t mac[6];
        ETH.macAddress(mac);
        char buf[13];
        sprintf(buf, "%02X%02X%02X%02X%02X%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return String(buf);
    }


    void resetETHPHY() {
        pinMode(PHY_RST_PIN, OUTPUT);
        digitalWrite(PHY_RST_PIN, LOW);
        delay(100);
        digitalWrite(PHY_RST_PIN, HIGH);
        delay(1000);
    }

    // ================= 以太网初始化（支持DHCP和静态IP） =================
    void setupEthernet() {
        if (ethInitialized) return;

        Serial.println("[ETH] 初始化以太网...");
        resetETHPHY();

        // 先启动以太网，这是必须的第一步
        Serial.println("[ETH] 启动以太网硬件...");
        if (!ETH.begin()) {
            Serial.println("[ETH] ETH 初始化失败");
            return;
        }

        ethInitialized = true;
        Serial.println("[ETH] 以太网硬件初始化完成");

        // 等待一小段时间让ETH稳定
        delay(500);

        // 读取保存的网络配置
        Preferences prefs;
        prefs.begin("device_config", true);

        String ip = prefs.getString("net_ip", "0.0.0.0");
        String gateway = prefs.getString("net_gateway", "0.0.0.0");
        String subnet = prefs.getString("net_subnet", "255.255.255.0");
        String dns = prefs.getString("net_dns", "0.0.0.0");
        int isStatic = prefs.getInt("net_is_static", 0);

        prefs.end();

        Serial.printf("[ETH] 读取配置: ip=%s, isStatic=%d\n", ip.c_str(), isStatic);

        // 如果配置了静态IP，就配置静态IP
        if (isStatic == 1 && ip != "0.0.0.0") {
            Serial.println("[ETH] 配置静态IP模式");

            IPAddress configIP, configGateway, configSubnet, configDNS;
            configIP.fromString(ip.c_str());
            configGateway.fromString(gateway.c_str());
            configSubnet.fromString(subnet.c_str());

            if (dns != "0.0.0.0") {
                configDNS.fromString(dns.c_str());
            } else {
                configDNS = configGateway; // 默认使用网关作为DNS
            }

            // 在 ETH.begin() 之后配置静态IP
            Serial.printf("[ETH] 配置IP: %s\n", ip.c_str());
            if (ETH.config(configIP, configGateway, configSubnet, configDNS)) {
                Serial.println("[ETH] 静态IP配置成功");
                currentMode = NET_MODE_STATIC;
                staticIP = configIP;
                staticGateway = configGateway;
                staticSubnet = configSubnet;
                staticDNS = configDNS;
            } else {
                Serial.println("[ETH] 静态IP配置失败，使用DHCP");
                currentMode = NET_MODE_DHCP;
            }
        } else {
            Serial.println("[ETH] 使用DHCP模式");
            currentMode = NET_MODE_DHCP;
        }

        // 设置clientId
        clientId = getEthMacNoColon();
        Serial.printf("[ETH] 设备ID: %s\n", clientId.c_str());
    }

    // ================= 等待IP获取 =================
    void waitForIP() {
        // 检查是否是静态IP模式
        Preferences prefs;
        prefs.begin("device_config", true);
        String ip = prefs.getString("net_ip", "0.0.0.0");
        int isStatic = prefs.getInt("net_is_static", 0);
        prefs.end();

        if (isStatic == 1 && ip != "0.0.0.0") {
            // 静态IP模式
            Serial.println("[NET] 静态IP模式，等待网络就绪...");

            unsigned long start = millis();
            bool gotIP = false;

            while (millis() - start < 10000) { // 等待10秒，缩短时间
                if (ETH.linkUp()) {
                    IPAddress currentIP = ETH.localIP();
                    if (currentIP != IPAddress(0, 0, 0, 0)) {
                        Serial.printf("[NET] 网络就绪，IP: %s\n", currentIP.toString().c_str());
                        Serial.printf("[NET] 网关: %s\n", ETH.gatewayIP().toString().c_str());
                        Serial.printf("[NET] 子网掩码: %s\n", ETH.subnetMask().toString().c_str());

                        IPAddress dns1 = ETH.dnsIP(0);
                        if (dns1 != IPAddress(0, 0, 0, 0)) {
                            Serial.printf("[NET] DNS: %s\n", dns1.toString().c_str());
                        }

                        gotIP = true;
                        break;
                    }
                }
                delay(500);
                Serial.print(".");
            }

            if (!gotIP) {
                Serial.println("\n[NET] 静态IP连接超时！");
                Serial.println("[NET] 检查网络连接或配置是否正确");
            }
        } else {
            // DHCP模式
            Serial.println("[DHCP] 等待DHCP分配IP地址...");

            unsigned long start = millis();
            bool gotIP = false;

            while (millis() - start < 15000) { // 等待15秒
                if (ETH.linkUp()) {
                    IPAddress currentIP = ETH.localIP();
                    if (currentIP != IPAddress(0, 0, 0, 0)) {
                        Serial.printf("[DHCP] 已获取IP: %s\n", currentIP.toString().c_str());
                        Serial.printf("[DHCP] 网关: %s\n", ETH.gatewayIP().toString().c_str());
                        Serial.printf("[DHCP] 子网掩码: %s\n", ETH.subnetMask().toString().c_str());

                        IPAddress dns1 = ETH.dnsIP(0);
                        if (dns1 != IPAddress(0, 0, 0, 0)) {
                            Serial.printf("[DHCP] DNS: %s\n", dns1.toString().c_str());
                        }

                        gotIP = true;
                        break;
                    }
                }
                delay(500);
                Serial.print(".");
            }

            if (!gotIP) {
                Serial.println("\n[DHCP] DHCP获取超时！");
                Serial.println("[DHCP] 检查DHCP服务器和网络连接");
            }
        }
    }

    void checkEthernetLink() {
        bool link = ETH.linkUp();

        if (link && !ethLinkUp) {
            ethLinkUp = true;
            currentNet = NET_ETH;

            Serial.printf("[NET] 以太网链路已建立，模式: %s\n",
                         currentMode == NET_MODE_DHCP ? "DHCP" : "静态IP");

            // 等待IP分配
            waitForIP();

            IPAddress ip = ETH.localIP();
            if (ip != IPAddress(0, 0, 0, 0)) {
                Serial.printf("[NET] IP地址: %s\n", ip.toString().c_str());
                Serial.printf("[NET] 网关: %s\n", ETH.gatewayIP().toString().c_str());
                Serial.printf("[NET] 子网掩码: %s\n", ETH.subnetMask().toString().c_str());
            }

        } else if (!link && ethLinkUp) {
            ethLinkUp = false;
            currentNet = NET_NONE;
            Serial.println("[NET] 以太网链路断开");
        }
    }

    void checkNetwork() {
        setupEthernet();
        checkEthernetLink();
    }

    void taskNetwork(void* pvParameters) {
        while (true) {
            checkNetwork();
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }

    // ================= 公共接口 =================

    void init() {
        Serial.println("[NET] 启动网络管理...");

        // 初始化clientId
        clientId = getEthMacNoColon();
        Serial.printf("[NET] 设备ID: %s\n", clientId.c_str());

        // 启动网络任务
        xTaskCreatePinnedToCore(taskNetwork, "NetworkTask", 10000, NULL, 1, NULL, 1);
    }

    // ================= 网络信息获取 =================
    bool isLinkUp() {
        return ethLinkUp;
    }

    IPAddress localIP() {
        if (currentNet == NET_ETH) {
            return ETH.localIP();
        } else if (currentNet == NET_WIFI) {
            return WiFi.localIP();
        }
        return IPAddress(0, 0, 0, 0);
    }

    IPAddress gatewayIP() {
        if (currentNet == NET_ETH) {
            return ETH.gatewayIP();
        } else if (currentNet == NET_WIFI) {
            return WiFi.gatewayIP();
        }
        return IPAddress(0, 0, 0, 0);
    }

    IPAddress subnetMask() {
        if (currentNet == NET_ETH) {
            return ETH.subnetMask();
        } else if (currentNet == NET_WIFI) {
            return WiFi.subnetMask();
        }
        return IPAddress(0, 0, 0, 0);
    }

    // ================= 获取当前模式 =================
    NetMode getCurrentMode() {
        return currentMode;
    }

    // ========== 设置静态IP ==========
    void setStaticIP(const IPAddress& ip, const IPAddress& gateway,
                     const IPAddress& subnet, const IPAddress& dns) {
        currentMode = NET_MODE_STATIC;
        staticIP = ip;
        staticGateway = gateway;
        staticSubnet = subnet;
        staticDNS = dns;

        Serial.printf("[NET] 静态IP已设置: %s\n", ip.toString().c_str());

        // 保存到配置文件
        Preferences prefs;
        prefs.begin("device_config", false);
        prefs.putString("net_ip", ip.toString());
        prefs.putString("net_gateway", gateway.toString());
        prefs.putString("net_subnet", subnet.toString());
        prefs.putString("net_dns", dns.toString());
        prefs.putInt("net_is_static", 1);
        prefs.end();
    }

    // ========== 设置DHCP ==========
    void setDHCP() {
        currentMode = NET_MODE_DHCP;

        // 保存到配置文件
        Preferences prefs;
        prefs.begin("device_config", false);
        prefs.putString("net_ip", "0.0.0.0");
        prefs.putString("net_gateway", "0.0.0.0");
        prefs.putString("net_subnet", "255.255.255.0");
        prefs.putString("net_dns", "0.0.0.0");
        prefs.putInt("net_is_static", 0);
        prefs.end();

        Serial.println("[NET] 已设置为DHCP模式");
    }

    // ========== 应用并重启 ==========
    void applyAndReboot() {
        Serial.println("[NET] 应用网络配置并重启...");
        delay(1000);
        ESP.restart();
    }

    // ================= 打印网络信息 =================
    void printNetworkInfo() {
        Serial.println("\n=== 网络信息 ===");
        Serial.printf("配置模式: %s\n",
                      currentMode == NET_MODE_DHCP ? "DHCP" : "静态IP");

        if (currentNet != NET_NONE) {
            Serial.printf("IP地址: %s\n", localIP().toString().c_str());
            Serial.printf("网关: %s\n", gatewayIP().toString().c_str());
            Serial.printf("子网掩码: %s\n", subnetMask().toString().c_str());

            if (currentNet == NET_ETH) {
                uint8_t mac[6];
                ETH.macAddress(mac);
                Serial.printf("MAC地址: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }
        }

        if (currentMode == NET_MODE_STATIC) {
            Serial.printf("配置的静态IP: %s\n", staticIP.toString().c_str());
        }

        Serial.printf("设备ID: %s\n", clientId.c_str());
        Serial.println("================\n");
    }

} // namespace NetManager