#pragma once
#include <Arduino.h>
#include <vector>

namespace AutomationManager {

/**
 * @brief 自动化规则结构
 * 
 * 每个规则包含三个部分：
 * 
 * 1. **时间段控制（可选）**
 *    - 限定规则只在特定星期几、某个时间范围内生效。
 * 
 * 2. **条件触发（可选）**
 *    - 当一个或多个传感器满足特定条件时触发，例如：
 *      - 温度 > 30℃
 *      - 湿度 < 50%
 *      - 开关状态 == 1
 * 
 * 3. **动作列表**
 *    - 当规则满足时执行的控制动作，例如：
 *      - 打开某个继电器
 *      - 关闭某个风扇
 * 
 * 规则由唯一 `id` 标识，用于通过 MQTT 消息覆盖或删除。
 */
struct AutomationRule {
    String id;              ///< 唯一规则 ID，用于更新或删除

    // ----- 时间段控制 -----
    bool useTimeRange;      ///< 是否启用时间范围判断
    uint8_t weekDays;       ///< 周期位掩码，bit0=周一 ... bit6=周日（1 表示生效）
    uint8_t startHour;      ///< 开始小时（0~23）
    uint8_t startMinute;    ///< 开始分钟（0~59）
    uint8_t endHour;        ///< 结束小时（0~23）
    uint8_t endMinute;      ///< 结束分钟（0~59）

    // ----- 条件触发 -----
    bool useConditions;     ///< 是否启用条件判断
    struct Condition {
        uint8_t slaveId;    ///< 目标从机地址（Modbus 从站 ID）
        uint16_t regAddr;   ///< 寄存器地址（线圈或保持寄存器等）
        enum Op { GREATER, LESS, EQUAL } op;  ///< 比较操作符：>、<、==
        float threshold;    ///< 阈值，触发条件依据
    };
    std::vector<Condition> conditions; ///< 条件列表，全部满足才触发动作

    // ----- 动作列表 -----
    struct Action {
        uint8_t slaveId;    ///< 目标从机地址
        uint16_t regAddr;   ///< 寄存器地址
        int sdata;          ///< 写入值（例如 0=关, 1=开）
    };
    std::vector<Action> actions; ///< 要执行的动作列表
};

/**
 * @brief 当前已注册的自动化规则列表
 */
extern std::vector<AutomationRule> rules;

/**
 * @brief 初始化自动化管理器
 * 
 * 清空规则与缓存，准备接受 MQTT 下发规则。
 * 应在系统启动时调用一次。
 */
void init();

/**
 * @brief 启动自动化任务（FreeRTOS）
 * 
 * 自动化任务会：
 * - 每隔固定时间（如 5 秒）遍历所有规则；
 * - 检查当前时间与条件是否满足；
 * - 如果满足，则执行对应的控制动作。
 */
void startTasks();

/**
 * @brief 处理 MQTT 下发的规则消息
 * 
 * 支持的消息格式：
 * ```json
 * {
 *   "action": "add" | "remove",
 *   "id": "rule_001",
 *   "useTimeRange": true,
 *   "week": [1,2,3,4,5],
 *   "time_start": "08:00",
 *   "time_end": "18:00",
 *   "useConditions": true,
 *   "conditions": [
 *     {"slaveId":1,"regAddr":0,"op":">","threshold":30}
 *   ],
 *   "actions": [
 *     {"slaveId":2,"regAddr":0,"sdata":1}
 *   ]
 * }
 * ```
 * - `"add"`：添加或覆盖规则；
 * - `"remove"`：删除指定 ID 的规则（未指定时删除全部）。
 */
void handleMqttMessage(const String& message);

/**
 * @brief 当传感器上报新数据时调用
 * 
 * @param slaveId 传感器所在的 Modbus 从机地址
 * @param regAddr 传感器寄存器地址
 * @param value    当前采集值
 * @param fromAutomation 是否来自自动化动作
 * 
 * - 若 `fromAutomation == false`，则更新缓存并触发规则判断；
 * - 若 `fromAutomation == true`，表示这是自动化动作反馈，避免死循环触发。
 */
void onSensorData(uint8_t slaveId, uint16_t regAddr, int value, bool fromAutomation = false);

/**
 * @brief 自动化任务主循环
 * 
 * 由 FreeRTOS 调度执行，定期：
 * - 检查当前时间是否在各规则有效期内；
 * - 比对传感器缓存数据是否满足条件；
 * - 若满足，则调用 `ModbusManager::writeCoil()` 执行动作。
 */
void taskAutomation(void* pvParameters);

/**
 * @brief 手动触发一次规则检查（不等待周期任务）
 * 
 * 用于测试或外部主动触发逻辑，
 * 会立即遍历所有规则并执行符合条件的动作。
 */
void triggerOnce();



}
