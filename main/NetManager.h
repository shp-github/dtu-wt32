#pragma once
#include <Arduino.h>
#include <IPAddress.h>

namespace NetManager {

    /**
     * @brief 网络类型枚举
     */
    enum NetType { NET_NONE, NET_ETH, NET_WIFI };

    /**
     * @brief 网络配置模式
     */
    enum NetMode { NET_MODE_DHCP, NET_MODE_STATIC };

    /**
     * @brief 网络配置结构体
     */
    struct NetConfig {
        NetMode mode = NET_MODE_DHCP;
        IPAddress ip;
        IPAddress gateway;
        IPAddress subnet;
        IPAddress dns;
    };

    /* ================= 全局变量 ================= */
    extern NetType currentNet;
    extern String clientId;

    /* ================= 核心功能 ================= */
    void init();
    void taskNetwork(void* pvParameters);

    /* ================= 网络状态 ================= */
    bool isLinkUp();
    IPAddress localIP();
    IPAddress gatewayIP();
    IPAddress subnetMask();
    NetMode getCurrentMode();
    String getMDNSHostname();

    /* ================= 网络配置 ================= */
    void setDHCP();
    void setStaticIP(const IPAddress& ip, const IPAddress& gateway,
                     const IPAddress& subnet, const IPAddress& dns = IPAddress(0,0,0,0));
    void applyAndReboot();

    /* ================= 诊断工具 ================= */
    void printNetworkInfo();
    void restartEthernet();

} // namespace NetManager