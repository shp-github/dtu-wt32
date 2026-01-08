#include "SerialManager.h"
#include "ConfigManager.h"

#define UART1_RX 5
#define UART1_TX 17
#define UART2_RX 16
#define UART2_TX 4

HardwareSerial SerialManager::Serial1(1);
HardwareSerial SerialManager::Serial2(2);

namespace SerialManager {

UartConfig uart1;
UartConfig uart2;

static uint32_t getSerialConfig(int dataBits, ParityType parity, int stopBits) {
    if (dataBits == 8) {
        if (parity == ParityType::NONE && stopBits == 1) return SERIAL_8N1;
        if (parity == ParityType::EVEN && stopBits == 1) return SERIAL_8E1;
        if (parity == ParityType::ODD  && stopBits == 1) return SERIAL_8O1;
        if (parity == ParityType::NONE && stopBits == 2) return SERIAL_8N2;
        if (parity == ParityType::EVEN && stopBits == 2) return SERIAL_8E2;
        if (parity == ParityType::ODD  && stopBits == 2) return SERIAL_8O2;
    }
    return SERIAL_8N1;
}

static void startUart(HardwareSerial &serial, const char *name, const UartConfig &cfg, int rx, int tx) {
    if (!cfg.enabled) {
        serial.end();
        Serial.printf("[SerialManager] %s 已禁用\n", name);
        return;
    }

    uint32_t config = getSerialConfig(cfg.dataBits, cfg.parity, cfg.stopBits);
    serial.begin(cfg.baud, config, rx, tx);

    Serial.printf("[SerialManager] %s 已启动 → RX=%d TX=%d 波特率=%d 数据位=%d 停止位=%d 校验=%s 用途=%s\n",
                  name, rx, tx, cfg.baud, cfg.dataBits, cfg.stopBits,
                  (cfg.parity==ParityType::NONE?"NONE":cfg.parity==ParityType::EVEN?"EVEN":"ODD"),
                  cfg.name.c_str());
}

void init() {
    Serial.println("[SerialManager] ===== 串口初始化开始 =====");

    const ConfigManager::DeviceConfig &cfg = ConfigManager::getConfig();
    uart1 = cfg.uart1;
    uart2 = cfg.uart2;

    startUart(Serial1, "Serial1", uart1, UART1_RX, UART1_TX);
    startUart(Serial2, "Serial2", uart2, UART2_RX, UART2_TX);

    Serial.println("[SerialManager] ===== 串口初始化完成 =====");
}

void applyConfig() {
    const ConfigManager::DeviceConfig &cfg = ConfigManager::getConfig();
    uart1 = cfg.uart1;
    uart2 = cfg.uart2;

    startUart(Serial1, "Serial1", uart1, UART1_RX, UART1_TX);
    startUart(Serial2, "Serial2", uart2, UART2_RX, UART2_TX);
}

} // namespace SerialManager
