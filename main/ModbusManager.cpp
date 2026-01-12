#include "ModbusManager.h"
#include "NetManager.h"
#include <ModbusMaster.h>
#include <HardwareSerial.h>
#include "MqttManager.h"
#include <ArduinoJson.h>
#include "AutomationManager.h"
#include "SerialManager.h"
#include "NetworkManager.h"

using namespace NetManager;

// ================== 硬件配置 ==================
HardwareSerial &modbusSerial = SerialManager::Serial1;
ModbusMaster node;

// ================== 全局配置变量 ==================
namespace ModbusManager {
    //轮询状态标志
    static bool isPollingRoundActive = false;
    // 当前配置
    static ModbusConfig currentConfig;
    // 任务句柄
    static TaskHandle_t modbusTaskHandle = NULL;
    // 任务运行标志
    static bool taskRunning = false;
}

// 定义通道标识常量
#define CHANNEL_MQTT 0
#define CHANNEL_NET_1 1
#define CHANNEL_NET_2 2
#define CHANNEL_NET_3 3

// ================== 辅助函数 - 带重试机制的Modbus读取 ==================

// 根据功能码执行Modbus读取（带重试机制）
bool ModbusManager::executeModbusRead(const ModbusManager::Command &cmd, int maxRetries) {
    // 确保有要读取的寄存器
    if (cmd.s == 0) {
        return false;
    }

    // 确保从机地址有效
    if (cmd.a == 0 || cmd.a > 247) {
        Serial.printf("[MODBUS] 无效的从机地址: %d\n", cmd.a);
        return false;
    }

    // 初始化Modbus节点
    node.begin(cmd.a, modbusSerial);

    // 根据功能码选择读取方式
    bool isCoil = (cmd.f == "01" || cmd.f == "02");
    bool isRegister = (cmd.f == "03" || cmd.f == "04");

    if (!isCoil && !isRegister) {
        Serial.printf("[MODBUS] 不支持的功能码: %s\n", cmd.f.c_str());
        return false;
    }

    uint8_t result;
    String operationType;

    // 重试循环
    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        // 执行读取操作
        if (isCoil) {
            result = node.readCoils(cmd.r, cmd.s);
            operationType = "线圈";
        } else {
            result = node.readHoldingRegisters(cmd.r, cmd.s);
            operationType = "寄存器";
        }

        if (result == node.ku8MBSuccess) {
            // 读取成功
            if (attempt > 1) {
                Serial.printf("[MODBUS] 读取成功(第%d次尝试): 从机=%d, %s=%d-%d, 数量=%d\n",
                             attempt, cmd.a, operationType.c_str(),
                             cmd.r, cmd.r + cmd.s - 1,
                             cmd.s);
            } else {
                Serial.printf("[MODBUS] 读取成功: 从机=%d, %s=%d-%d, 数量=%d\n",
                             cmd.a, operationType.c_str(),
                             cmd.r, cmd.r + cmd.s - 1,
                             cmd.s);
            }

            // 处理读取到的数据
            processModbusData(cmd, isCoil);
            return true;
        }

        // 读取失败，记录错误
        Serial.printf("[MODBUS] 读取失败(第%d次尝试): 从机=%d, %s=%d, 错误码=%d\n",
                     attempt, cmd.a, operationType.c_str(),
                     cmd.r, result);

        // 如果不是最后一次尝试，延迟后重试
        if (attempt < maxRetries) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }

    // 所有重试都失败
    Serial.printf("[MODBUS] 从机 %d %s %d 读取失败, 达到最大重试次数 %d\n",
                 cmd.a, (isCoil ? "线圈" : "寄存器"), cmd.r, maxRetries);
    return false;
}

// ================== 多通道发布函数 ==================

// 检查通道是否可用
static bool isChannelAvailable(int channelId) {
    if (channelId == CHANNEL_MQTT) {
        return true;
    }
    return NetworkManager::isChannelConnected(channelId-1);
}

// 发布数据到指定通道
static bool publishToChannel(int channelId, uint8_t slaveId, uint16_t regAddr, uint32_t value, const String& key) {
    // 检查通道是否可用
    if (!isChannelAvailable(channelId)) {
        Serial.printf("[MODBUS] 通道%d不可用，跳过发送\n", channelId);

        // 添加调试信息
        if (channelId == CHANNEL_MQTT) {
            Serial.println("[MODBUS] MQTT通道未连接");
        } else if (channelId >= CHANNEL_NET_1 && channelId <= CHANNEL_NET_3) {
            ConfigManager::ChannelConfig& config = ConfigManager::deviceConfig.channels[channelId];
            Serial.printf("[MODBUS] 网络通道%d配置: enabled=%d, protocol=%s, target=%s:%d\n",
                         channelId, config.enabled, config.protocol.c_str(),
                         config.target.c_str(), config.port);
        }
        return false;
    }

    // 创建JSON数据
    StaticJsonDocument<200> doc;
    JsonArray arr = doc.to<JsonArray>();
    JsonObject obj = arr.createNestedObject();
    obj["sensor_device_id"] = slaveId;
    obj["port_id"] = regAddr;
    obj["sdata"] = value;
    if (!key.isEmpty()) {
        obj["key"] = key;
    }

    String payload;
    serializeJson(doc, payload);

    bool success = false;

    switch (channelId) {
        case CHANNEL_MQTT:  // 通道0: MQTT
            Serial.printf("[MODBUS] 发送到MQTT通道: %s\n", payload.c_str());
            MqttManager::publish("/dev/coo/" + NetManager::clientId, payload);
            success = true;  // MQTT发布通常假设成功
            break;

        case CHANNEL_NET_1:  // 通道1: 网络通道1
        case CHANNEL_NET_2:  // 通道2: 网络通道2
        case CHANNEL_NET_3:  // 通道3: 网络通道3
            Serial.printf("[MODBUS] 发送到网络通道%d: %s\n", channelId, payload.c_str());
            success = NetworkManager::sendToChannel(channelId - 1,
                                                   (const uint8_t*)payload.c_str(),
                                                   payload.length());
            break;

        default:
            Serial.printf("[MODBUS] 错误: 未知的通道ID %d\n", channelId);
            break;
    }

    if (success) {
        Serial.printf("[MODBUS] 通道%d上报成功: 从机=%d, 地址=0x%04X, 值=%u\n",
                     channelId, slaveId, regAddr, value);
    } else {
        Serial.printf("[MODBUS] 通道%d上报失败\n", channelId);
    }

    return success;
}

// 发布数据到所有配置的通道
static void publishToAllChannels(uint8_t slaveId, uint16_t regAddr, uint32_t value, const String& key) {
    // 重新解析输出源配置
    bool useChannel0 = false;  // 0: MQTT通道
    bool useChannel1 = false;  // 1: 网络通道1
    bool useChannel2 = false;  // 2: 网络通道2
    bool useChannel3 = false;  // 3: 网络通道3

    // 遍历配置的输出源
    for (int source : ModbusManager::currentConfig.outputSource) {
        // 直接使用outputSource中的值，0对应通道0，1对应通道1，以此类推
        switch (source) {
            case 0: useChannel0 = true; break;  // 通道0: MQTT
            case 1: useChannel1 = true; break;  // 通道1: 网络通道1
            case 2: useChannel2 = true; break;  // 通道2: 网络通道2
            case 3: useChannel3 = true; break;  // 通道3: 网络通道3
            default:
                Serial.printf("[MODBUS] 警告: 未知的输出源 %d\n", source);
                break;
        }
    }

    // 如果没有配置输出源，默认使用通道0(MQTT)
    if (ModbusManager::currentConfig.outputSource.empty()) {
        useChannel0 = true;
        Serial.println("[MODBUS] 输出源为空，默认使用通道0(MQTT)");
    }

    // 打印调试信息
    Serial.printf("[MODBUS] 当前启用: 通道0(MQTT)=%d, 通道1=%d, 通道2=%d, 通道3=%d\n",
                  useChannel0, useChannel1, useChannel2, useChannel3);

    // 统计成功数量
    int successCount = 0;
    int totalChannels = 0;

    // 发布到所有配置的通道
    if (useChannel0) {
        totalChannels++;
        if (publishToChannel(CHANNEL_MQTT, slaveId, regAddr, value, key)) {
            successCount++;
        }
    }

    if (useChannel1) {
        totalChannels++;
        if (publishToChannel(CHANNEL_NET_1, slaveId, regAddr, value, key)) {
            successCount++;
        }
    }

    if (useChannel2) {
        totalChannels++;
        if (publishToChannel(CHANNEL_NET_2, slaveId, regAddr, value, key)) {
            successCount++;
        }
    }

    if (useChannel3) {
        totalChannels++;
        if (publishToChannel(CHANNEL_NET_3, slaveId, regAddr, value, key)) {
            successCount++;
        }
    }

    // 输出发布统计
    if (totalChannels > 0) {
        Serial.printf("[MODBUS] 发布统计: 成功 %d/%d 个通道\n", successCount, totalChannels);
    }
}

// ================== 数据处理器函数 ==================

// 处理读取到的Modbus数据
void ModbusManager::processModbusData(const ModbusManager::Command &cmd, bool isCoil) {
    Serial.printf("[MODBUS] 处理数据: 从机=%d, 起始地址=0x%04X, 寄存器数量=%d\n",
                 cmd.a, cmd.r, cmd.s);

    // 遍历配置中的每个寄存器定义
    for (const auto& regDef : cmd.arr) {
        // 检查该寄存器定义是否在当前读取的范围内
        if (regDef.a < cmd.r || regDef.a + regDef.l - 1 > cmd.r + cmd.s - 1) {
            Serial.printf("[MODBUS] 警告: 寄存器定义超出读取范围 key=%s, addr=0x%04X, len=%u\n",
                         regDef.k.c_str(), regDef.a, regDef.l);
            continue;
        }

        // 根据长度计算值
        uint32_t value = 0;

        if (regDef.l == 1) {
            // 单寄存器
            int bufferIndex = regDef.a - cmd.r;
            value = node.getResponseBuffer(bufferIndex);
        }
        else if (regDef.l == 2) {
            // 双寄存器（32位数据）
            int bufferIndex1 = regDef.a - cmd.r;
            int bufferIndex2 = bufferIndex1 + 1;

            uint16_t lowWord = node.getResponseBuffer(bufferIndex1);
            uint16_t highWord = node.getResponseBuffer(bufferIndex2);

            // 根据字节序组合数据
            if (regDef.o == "ABCD") {
                value = ((uint32_t)highWord << 16) | lowWord;
            } else if (regDef.o == "CDAB") {
                value = ((uint32_t)lowWord << 16) | highWord;
            } else if (regDef.o == "BADC") {
                uint16_t swappedLow = ((lowWord & 0xFF) << 8) | ((lowWord >> 8) & 0xFF);
                uint16_t swappedHigh = ((highWord & 0xFF) << 8) | ((highWord >> 8) & 0xFF);
                value = ((uint32_t)swappedHigh << 16) | swappedLow;
            } else {
                value = ((uint32_t)highWord << 16) | lowWord;
            }
        }
        else {
            Serial.printf("[MODBUS] 错误: 不支持的长度 %u for key=%s\n",
                         regDef.l, regDef.k.c_str());
            continue;
        }

        // 发布到所有配置的输出通道
        publishToAllChannels(cmd.a, regDef.a, value, regDef.k);

        // 通知自动化模块
        AutomationManager::onSensorData(cmd.a, regDef.a, value, false);
    }
}

// ================== 应用配置 ==================
void ModbusManager::applyConfig(const ModbusConfig &cfg) {
    Serial.printf("[MODBUS] 应用配置: enabled=%d, interval=%d, commands=%zu\n",
                 cfg.enabled, cfg.interval, cfg.commands.size());

    // 显示输出通道配置
    if (!cfg.outputSource.empty()) {
        Serial.print("[MODBUS] 配置的输出源ID: ");
        for (int source : cfg.outputSource) {
            Serial.printf("%d ", source);
        }
        Serial.println();

        // 解释这些ID的含义
        Serial.println("[MODBUS] 通道映射: 0=MQTT, 1=网络通道1, 2=网络通道2, 3=网络通道3");
    }

    // 保存配置
    currentConfig = cfg;

    // 检查是否启用
    if (!cfg.enabled) {
        Serial.println("[MODBUS] Modbus 未启用");
        return;
    }

    // 检查是否有命令
    if (cfg.commands.empty()) {
        Serial.println("[MODBUS] 无命令配置");
        return;
    }

    // 创建新任务
    BaseType_t taskResult = xTaskCreatePinnedToCore(
        taskModbus,
        "ModbusTask",
        10000,
        NULL,
        1,
        &modbusTaskHandle,
        1
    );

    if (taskResult == pdPASS) {
        taskRunning = true;
        Serial.println("[MODBUS] 任务启动成功");
    } else {
        Serial.printf("[MODBUS] 任务创建失败，错误码=%d\n", taskResult);
    }
}

// ================== Modbus 轮询任务 ==================
void ModbusManager::taskModbus(void* pvParameters) {
    Serial.println("[MODBUS] Modbus轮询任务启动");

    while (true) {
        // 检查任务是否应该运行
        if (!taskRunning || !currentConfig.enabled || currentConfig.commands.empty()) {
            Serial.println("[MODBUS] 任务暂停等待...");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        // 等待上一轮完成
        if (isPollingRoundActive) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        // 标记轮询开始
        isPollingRoundActive = true;

        // 遍历所有命令
        bool anySuccess = false;
        for (const auto &cmd : currentConfig.commands) {
            if (executeModbusRead(cmd, 3)) {
                anySuccess = true;
            }
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        // 如果没有成功读取，延长等待时间
        if (!anySuccess) {
            Serial.println("[MODBUS] 本轮无成功读取，延长等待时间");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
        }

        // 根据配置的interval等待
        uint32_t delayTime = currentConfig.interval;
        if (delayTime < 1000) {
            delayTime = 1000;
        }
        vTaskDelay(delayTime / portTICK_PERIOD_MS);

        // 标记轮询结束
        isPollingRoundActive = false;
    }
}

// ================== 写单个线圈（带重试机制） ==================
bool ModbusManager::writeCoil(uint8_t slaveId, uint16_t regAddr, int sdata, int maxRetries) {
    node.begin(slaveId, modbusSerial);

    uint16_t value = (sdata == 1) ? 0x0001 : 0x0000;

    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        uint8_t result = node.writeSingleCoil(regAddr, value);

        if (result == node.ku8MBSuccess) {
            Serial.printf("[MODBUS] 从机 %d 线圈 %d 写入 %d 成功\n", slaveId, regAddr, value);
            return true;
        } else {
            node.readCoils(regAddr, 1);
            uint16_t readValue = node.getResponseBuffer(0);
            if (readValue == value) {
                return true;
            }
        }

        if (attempt < maxRetries) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }

    Serial.printf("[MODBUS] 从机 %d 线圈 %d 写入失败\n", slaveId, regAddr);
    return false;
}

bool ModbusManager::writeCoilAndReport(uint8_t slaveId, uint16_t regAddr, int sdata, int maxRetries) {
    bool success = writeCoil(slaveId, regAddr, sdata, maxRetries);
    if (success) {
        // 通知自动化模块
        AutomationManager::onSensorData(slaveId, regAddr, sdata, true);

        // 创建JSON数据
        StaticJsonDocument<200> doc;
        JsonArray arr = doc.to<JsonArray>();
        JsonObject obj = arr.createNestedObject();
        obj["sensor_device_id"] = slaveId;
        obj["port_id"] = regAddr;
        obj["sdata"] = sdata;

        String payload;
        serializeJson(doc, payload);

        // 重新解析输出源配置
        bool useChannel0 = false;  // 0: MQTT通道
        bool useChannel1 = false;  // 1: 网络通道1
        bool useChannel2 = false;  // 2: 网络通道2
        bool useChannel3 = false;  // 3: 网络通道3

        for (int source : currentConfig.outputSource) {
            switch (source) {
                case 0: useChannel0 = true; break;
                case 1: useChannel1 = true; break;
                case 2: useChannel2 = true; break;
                case 3: useChannel3 = true; break;
            }
        }

        if (currentConfig.outputSource.empty()) {
            useChannel0 = true;
        }

        // 发布到通道
        if (useChannel0) {
            MqttManager::publish("/dev/coo/" + NetManager::clientId, payload);
        }

        if (useChannel1 && isChannelAvailable(CHANNEL_NET_1)) {
            NetworkManager::sendToChannel(CHANNEL_NET_1-1, (const uint8_t*)payload.c_str(), payload.length());
        }

        if (useChannel2 && isChannelAvailable(CHANNEL_NET_2)) {
            NetworkManager::sendToChannel(CHANNEL_NET_2-1, (const uint8_t*)payload.c_str(), payload.length());
        }

        if (useChannel3 && isChannelAvailable(CHANNEL_NET_3)) {
            NetworkManager::sendToChannel(CHANNEL_NET_3-1, (const uint8_t*)payload.c_str(), payload.length());
        }

        Serial.printf("[MODBUS] 写入并上报: 从机=%d, 线圈=%d, 值=%d\n",
                     slaveId, regAddr, sdata);
    }
    return success;
}

// ================== MQTT 消息处理 - 支持多通道反馈 ==================
void ModbusManager::handleMqttMessage(const String& message) {
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
        Serial.printf("[MODBUS] 解析 MQTT 消息失败: %s\n", error.c_str());
        return;
    }

    // 重新解析输出源配置
    bool useChannel0 = false;  // 0: MQTT通道
    bool useChannel1 = false;  // 1: 网络通道1
    bool useChannel2 = false;  // 2: 网络通道2
    bool useChannel3 = false;  // 3: 网络通道3

    for (int source : currentConfig.outputSource) {
        switch (source) {
            case 0: useChannel0 = true; break;
            case 1: useChannel1 = true; break;
            case 2: useChannel2 = true; break;
            case 3: useChannel3 = true; break;
        }
    }

    if (currentConfig.outputSource.empty()) {
        useChannel0 = true;
    }

    // 判断消息是单条还是多条
    if (doc.is<JsonObject>()) {
        // 单条消息处理
        JsonObject msg = doc.as<JsonObject>();
        int sensorId = msg["sensor_device_id"];
        int portId = msg["port_id"];
        int sdata = msg["sdata"];

        bool success = writeCoil(sensorId, portId, sdata);
        if (success) {
            // 创建反馈消息
            StaticJsonDocument<200> respDoc;
            JsonArray arr = respDoc.to<JsonArray>();
            JsonObject obj = arr.createNestedObject();
            obj["sensor_device_id"] = sensorId;
            obj["port_id"] = portId;
            obj["sdata"] = sdata;

            String payload;
            serializeJson(respDoc, payload);

            if (useChannel0) {
                MqttManager::publish("/dev/coo/" + NetManager::clientId, payload);
            }

            if (useChannel1 && isChannelAvailable(CHANNEL_NET_1)) {
                NetworkManager::sendToChannel(CHANNEL_NET_1-1, (const uint8_t*)payload.c_str(), payload.length());
            }

            if (useChannel2 && isChannelAvailable(CHANNEL_NET_2)) {
                NetworkManager::sendToChannel(CHANNEL_NET_2-1, (const uint8_t*)payload.c_str(), payload.length());
            }

            if (useChannel3 && isChannelAvailable(CHANNEL_NET_3)) {
                NetworkManager::sendToChannel(CHANNEL_NET_3-1, (const uint8_t*)payload.c_str(), payload.length());
            }

            // 通知自动化模块
            AutomationManager::onSensorData(sensorId, portId, sdata, false);
        }
    } else if (doc.is<JsonArray>()) {
        // 多条消息处理
        JsonArray msgArray = doc.as<JsonArray>();
        for (JsonObject msg : msgArray) {
            int sensorId = msg["sensor_device_id"];
            int portId = msg["port_id"];
            int sdata = msg["sdata"];

            bool success = writeCoil(sensorId, portId, sdata);
            if (success) {
                // 创建反馈消息
                StaticJsonDocument<200> respDoc;
                JsonArray arr = respDoc.to<JsonArray>();
                JsonObject obj = arr.createNestedObject();
                obj["sensor_device_id"] = sensorId;
                obj["port_id"] = portId;
                obj["sdata"] = sdata;

                String payload;
                serializeJson(respDoc, payload);

                if (useChannel0) {
                    MqttManager::publish("/dev/coo/" + NetManager::clientId, payload);
                }

                if (useChannel1 && isChannelAvailable(CHANNEL_NET_1)) {
                    NetworkManager::sendToChannel(CHANNEL_NET_1-1, (const uint8_t*)payload.c_str(), payload.length());
                }

                if (useChannel2 && isChannelAvailable(CHANNEL_NET_2)) {
                    NetworkManager::sendToChannel(CHANNEL_NET_2-1, (const uint8_t*)payload.c_str(), payload.length());
                }

                if (useChannel3 && isChannelAvailable(CHANNEL_NET_3)) {
                    NetworkManager::sendToChannel(CHANNEL_NET_3-1, (const uint8_t*)payload.c_str(), payload.length());
                }

                // 通知自动化模块
                AutomationManager::onSensorData(sensorId, portId, sdata, false);
            }
        }
    }
}