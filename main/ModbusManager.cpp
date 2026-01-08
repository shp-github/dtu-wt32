#include "ModbusManager.h"
#include "NetManager.h"
#include <ModbusMaster.h>
#include <HardwareSerial.h>
#include "MqttManager.h"
#include <ArduinoJson.h>
#include "AutomationManager.h"
#include "SerialManager.h"

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

// ================== 辅助函数 - 带重试机制的Modbus读取 ==================

// 根据功能码执行Modbus读取（带重试机制）
bool ModbusManager::executeModbusRead(const ModbusManager::Command &cmd, int maxRetries = 3) {
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

        // 立即输出result值
        Serial.printf("[MODBUS] 返回结果: result = %u\n", result);
        Serial.printf("[MODBUS] 期望值: ku8MBSuccess = %u\n", node.ku8MBSuccess);

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

// ================== 数据处理器函数 ==================

// 处理读取到的Modbus数据
void ModbusManager::processModbusData(const ModbusManager::Command &cmd, bool isCoil) {
    Serial.printf("[MODBUS] 处理数据: 从机=%d, 起始地址=0x%04X, 寄存器数量=%d\n",
                 cmd.a, cmd.r, cmd.s);

    // 输出寄存器映射信息
    Serial.printf("[MODBUS] 寄存器映射 (数量: %zu):\n", cmd.arr.size());
    for (size_t i = 0; i < cmd.arr.size(); ++i) {
        const auto& m = cmd.arr[i];
        Serial.printf("  [%zu] key: \"%s\", 地址: 0x%04X, 长度: %u, 字节序: %s\n",
                     i,
                     m.k.c_str(),
                     m.a,
                     m.l,
                     m.o.c_str());
    }

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

            Serial.printf("[MODBUS] 单寄存器: key=%s, addr=0x%04X, 值=0x%04X (%u)\n",
                         regDef.k.c_str(), regDef.a, value, value);
        }
        else if (regDef.l == 2) {
            // 双寄存器（32位数据）
            int bufferIndex1 = regDef.a - cmd.r;
            int bufferIndex2 = bufferIndex1 + 1;

            uint16_t lowWord = node.getResponseBuffer(bufferIndex1);
            uint16_t highWord = node.getResponseBuffer(bufferIndex2);

            // 根据字节序组合数据
            if (regDef.o == "ABCD") {
                // 大端序: 高字在前，低字在后
                value = ((uint32_t)highWord << 16) | lowWord;
            } else if (regDef.o == "CDAB") {
                // 小端序: 低字在前，高字在后
                value = ((uint32_t)lowWord << 16) | highWord;
            } else if (regDef.o == "BADC") {
                // 字节交换
                uint16_t swappedLow = ((lowWord & 0xFF) << 8) | ((lowWord >> 8) & 0xFF);
                uint16_t swappedHigh = ((highWord & 0xFF) << 8) | ((highWord >> 8) & 0xFF);
                value = ((uint32_t)swappedHigh << 16) | swappedLow;
            } else {
                // 默认使用大端序
                value = ((uint32_t)highWord << 16) | lowWord;
                Serial.printf("[MODBUS] 警告: 未知字节序 '%s', 使用默认大端序\n", regDef.o.c_str());
            }

            Serial.printf("[MODBUS] 双寄存器: key=%s, addr=0x%04X, 字[0]=0x%04X, 字[1]=0x%04X, 组合值=0x%08X (%u)\n",
                         regDef.k.c_str(), regDef.a, lowWord, highWord, value, value);
        }
        else if (regDef.l == 4) {
            // 四寄存器（64位数据）
            int bufferIndex1 = regDef.a - cmd.r;
            int bufferIndex2 = bufferIndex1 + 1;
            int bufferIndex3 = bufferIndex1 + 2;
            int bufferIndex4 = bufferIndex1 + 3;

            uint16_t word1 = node.getResponseBuffer(bufferIndex1);
            uint16_t word2 = node.getResponseBuffer(bufferIndex2);
            uint16_t word3 = node.getResponseBuffer(bufferIndex3);
            uint16_t word4 = node.getResponseBuffer(bufferIndex4);

            // 注意：这里处理的是32位值，64位需要特殊处理
            // 根据字节序组合数据
            if (regDef.o == "ABCD") {
                // ABCD: 字1为最高位，字4为最低位
                value = ((uint32_t)word1 << 16) | word2;
                // 如果需要64位，这里需要返回64位值
                Serial.printf("[MODBUS] 四寄存器(32位): key=%s, addr=0x%04X, 使用前两字\n",
                             regDef.k.c_str(), regDef.a);
            } else {
                // 默认使用前两个寄存器
                value = ((uint32_t)word1 << 16) | word2;
            }
        }
        else {
            Serial.printf("[MODBUS] 错误: 不支持的长度 %u for key=%s\n",
                         regDef.l, regDef.k.c_str());
            continue;
        }

        // 上报MQTT
        StaticJsonDocument<200> doc;
        JsonArray arr = doc.to<JsonArray>();
        JsonObject obj = arr.createNestedObject();
        obj["sensor_device_id"] = cmd.a;
        obj["port_id"] = regDef.a;  // 使用定义中的地址
        obj["sdata"] = value;
		if(regDef.k!=""){
			obj["key"] = regDef.k;
		}
        String payload;
        serializeJson(doc, payload);
        MqttManager::publish("/dev/coo/" + clientId, payload);

        Serial.printf("[MODBUS] 上报: 从机=%d, key=%s, 地址=0x%04X, 值=%u\n",
                     cmd.a, regDef.k.c_str(), regDef.a, value);

        // 通知自动化模块
        AutomationManager::onSensorData(cmd.a, regDef.a, value, false);
    }
}


// ================== 应用配置 ==================
void ModbusManager::applyConfig(const ModbusConfig &cfg) {
    Serial.printf("[MODBUS] 应用配置: enabled=%d, protocol=%s, input=%s, interval=%d, commands=%d\n",
                  cfg.enabled,
                  cfg.protocol.c_str(),
                  cfg.inputSource.c_str(),
                  cfg.interval,
                  cfg.commands.size());

    // 重要：先打印堆内存状态
    Serial.printf("[MODBUS] 堆内存: 可用=%d, 最小=%d\n",
                 ESP.getFreeHeap(), ESP.getMinFreeHeap());

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

    // 检查堆内存是否足够
    if (ESP.getFreeHeap() < 5000) { // 至少5KB空闲内存
        Serial.printf("[MODBUS] 警告：堆内存不足，仅剩%d字节\n", ESP.getFreeHeap());
        // 可以在这里尝试释放内存或重启
    }

    Serial.printf("[MODBUS] 一切正常，尝试启动，剩%d字节\n", ESP.getFreeHeap());

    // 创建新任务，并检查是否成功
    BaseType_t taskResult = xTaskCreatePinnedToCore(
        taskModbus,
        "ModbusTask",
        10000,
        NULL,
        1,      // 优先级
        &modbusTaskHandle,
        1       // 核心
    );

    if (taskResult == pdPASS) {
        taskRunning = true;
        Serial.println("[MODBUS] 任务启动成功");
    } else {
        Serial.printf("[MODBUS] 任务创建失败，错误码=%d\n", taskResult);
        Serial.printf("[MODBUS] 当前堆内存: %d\n", ESP.getFreeHeap());
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
            // 上一轮还在进行中，等待
            Serial.printf("[MODBUS] 等待上一轮完成");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        //禁用其他轮询任务
        isPollingRoundActive = true;

        // 遍历所有命令
        bool anySuccess = false;
        for (const auto &cmd : currentConfig.commands) {
            if (executeModbusRead(cmd)) {
                anySuccess = true;
            }
             // 每条命令间短暂
             vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        // 如果没有成功读取，延长等待时间
        if (!anySuccess) {
            Serial.println("[MODBUS] 本轮无成功读取，延长等待时间");
            vTaskDelay(10000 / portTICK_PERIOD_MS);
        }

        // 根据配置的interval等待（至少1000）
        uint32_t delayTime = currentConfig.interval;
        if (delayTime < 1000) {
            delayTime = 1000;
            Serial.println("[MODBUS] 确保至少1000ms间隔");
        }
        vTaskDelay(delayTime / portTICK_PERIOD_MS);

        //允许其他轮询任务继续执行
        isPollingRoundActive = false;

    }
}


// ================== 写单个线圈（带重试机制） - 保持原有代码 ==================
bool ModbusManager::writeCoil(uint8_t slaveId, uint16_t regAddr, int sdata, int maxRetries) {
    node.begin(slaveId, modbusSerial);

    uint16_t value = (sdata == 1) ? 0x0001 : 0x0000;  // 1 开, 0 关

    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        uint8_t result = node.writeSingleCoil(regAddr, value);

        if (result == node.ku8MBSuccess) {
            Serial.printf("[MODBUS] 从机 %d 线圈 %d 写入 %d 成功 (尝试 %d)\n", slaveId, regAddr, value, attempt);
            return true;
        } else {
            // 异常码时回读验证
            node.readCoils(regAddr, 1);
            uint16_t readValue = node.getResponseBuffer(0);
            if (readValue == value) {
                Serial.printf("[MODBUS] 从机 %d 线圈 %d 写入异常码 %d，但读回值正确 (%d, 尝试 %d)\n",
                              slaveId, regAddr, result, readValue, attempt);
                return true;
            } else {
                Serial.printf("[MODBUS] 从机 %d 线圈 %d 写入失败, 错误码: %d, 读回值: %d (尝试 %d)\n",
                              slaveId, regAddr, result, readValue, attempt);
            }
        }

        if (attempt < maxRetries) {
            vTaskDelay(100 / portTICK_PERIOD_MS); // 每次失败后短延迟
        }
    }

    // 所有尝试失败
    Serial.printf("[MODBUS] 从机 %d 线圈 %d 写入失败, 达到最大重试次数 %d\n", slaveId, regAddr, maxRetries);
    return false;
}

bool ModbusManager::writeCoilAndReport(uint8_t slaveId, uint16_t regAddr, int sdata, int maxRetries) {
    bool success = writeCoil(slaveId, regAddr, sdata, maxRetries);
    if (success) {
        // 通知自动化，但标记来源为自动化
        AutomationManager::onSensorData(slaveId, regAddr, sdata, true);

        // 上报 MQTT
        StaticJsonDocument<200> doc;
        JsonArray arr = doc.to<JsonArray>();
        JsonObject obj = arr.createNestedObject();
        obj["sensor_device_id"] = slaveId;
        obj["port_id"] = regAddr;
        obj["sdata"] = sdata;

        String payload;
        serializeJson(doc, payload);
        MqttManager::publish("/dev/coo/" + clientId, payload);
    }
    return success;
}


// ================== MQTT 消息处理 - 保持原有代码 ==================
void ModbusManager::handleMqttMessage(const String& message) {

    // 增大 JSON 解析文档大小以支持多个对象
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
        Serial.printf("[MODBUS] 解析 MQTT 消息失败: %s\n", error.c_str());
        return;
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

            // 反馈给服务器
            StaticJsonDocument<200> respDoc;
            JsonArray arr = respDoc.to<JsonArray>();
            JsonObject obj = arr.createNestedObject();
            obj["sensor_device_id"] = sensorId;
            obj["port_id"] = portId;
            obj["sdata"] = sdata;

            String payload;
            serializeJson(respDoc, payload);
            MqttManager::publish("/dev/coo/" + clientId, payload);
            Serial.printf("[MODBUS] 控制成功, 回复: %s\n", payload.c_str());

             // 通知自动化模块
            AutomationManager::onSensorData(sensorId, portId, sdata, false);

        } else {
            Serial.printf("[MODBUS] 控制失败: 从机 %d, 寄存器 %d\n", sensorId, portId);
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


                // 反馈给服务器
                StaticJsonDocument<2048> respDoc;
                JsonArray arr = respDoc.to<JsonArray>();
                JsonObject obj = arr.createNestedObject();
                obj["sensor_device_id"] = sensorId;
                obj["port_id"] = portId;
                obj["sdata"] = sdata;

                String payload;
                serializeJson(respDoc, payload);
                MqttManager::publish("/dev/coo/" + clientId, payload);
                Serial.printf("[MODBUS] 控制成功, 回复: %s\n", payload.c_str());

                 // 通知自动化模块
                AutomationManager::onSensorData(sensorId, portId, sdata, false);

            } else {
                Serial.printf("[MODBUS] 控制失败: 从机 %d, 寄存器 %d\n", sensorId, portId);
            }
        }
    } else {
        // 处理解析失败的情况
        Serial.println("[MODBUS] 消息格式不正确");
    }
}

