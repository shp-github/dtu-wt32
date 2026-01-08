#include "AutomationManager.h"
//#include "ModbusManager.h"
#include <ArduinoJson.h>
#include <time.h>
#include <algorithm>

namespace AutomationManager {

void triggerOnce();

// 全局规则列表
std::vector<AutomationRule> rules;

// 保存最新传感器数据，用于条件触发
struct SensorData {
    uint8_t slaveId;
    uint16_t regAddr;
    float value;
};
std::vector<SensorData> sensorCache;

// ----------------- 初始化 -----------------
void init() {
    rules.clear();
    sensorCache.clear();
    Serial.println("[Automation] 自动化管理器初始化完成");
}

// ----------------- MQTT 下发规则解析 -----------------
void handleMqttMessage(const String& message) {
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, message);
    if (err) {
        Serial.printf("[Automation] 解析规则失败: %s\n", err.c_str());
        return;
    }

    String actionType = doc["action"];
    String ruleId = doc["id"] | ""; // 获取规则 id

    Serial.printf("[Automation] 收到规则消息: action=%s, id=%s\n", actionType.c_str(), ruleId.c_str());

    if (actionType == "add") {
        AutomationRule rule;
        rule.id = ruleId;

        // 时间段
        rule.useTimeRange = doc["useTimeRange"];
        rule.weekDays = 0;
        if (rule.useTimeRange) {
            for (JsonVariant dayVar : doc["week"].as<JsonArray>()) {
                int day = dayVar.as<int>();
                if (day >= 1 && day <= 7) rule.weekDays |= (1 << (day - 1));
            }
            String tstart = doc["time_start"];
            String tend = doc["time_end"];
            rule.startHour = tstart.substring(0,2).toInt();
            rule.startMinute = tstart.substring(3,5).toInt();
            rule.endHour = tend.substring(0,2).toInt();
            rule.endMinute = tend.substring(3,5).toInt();
        }

        // 条件触发
        rule.useConditions = doc["useConditions"];
        if (rule.useConditions) {
            for (JsonObject cond : doc["conditions"].as<JsonArray>()) {
                AutomationRule::Condition c;
                c.slaveId = cond["slaveId"];
                c.regAddr = cond["regAddr"];
                String op = cond["op"].as<String>();
                if (op == ">") c.op = AutomationRule::Condition::GREATER;
                else if (op == "<") c.op = AutomationRule::Condition::LESS;
                else c.op = AutomationRule::Condition::EQUAL;
                c.threshold = cond["threshold"];
                rule.conditions.push_back(c);
            }
        }

        // 动作列表
        for (JsonObject act : doc["actions"].as<JsonArray>()) {
            AutomationRule::Action a;
            a.slaveId = act["slaveId"];
            a.regAddr = act["regAddr"];
            a.sdata = act["sdata"];
            rule.actions.push_back(a);
        }

        // 如果已有相同 id 的规则则覆盖
        auto it = std::find_if(rules.begin(), rules.end(),
                               [&](const AutomationRule &r){ return r.id == rule.id; });
        if (it != rules.end()) {
            *it = rule;
            Serial.printf("[Automation] 覆盖规则: %s\n", rule.id.c_str());
        } else {
            rules.push_back(rule);
            Serial.printf("[Automation] 添加规则: %s, 当前规则数: %d\n", rule.id.c_str(), (int)rules.size());
        }
    } 
    else if (actionType == "remove") {
        if (ruleId.length() > 0) {
            // 删除指定 id
            rules.erase(std::remove_if(rules.begin(), rules.end(),
                                       [&](const AutomationRule &r){ return r.id == ruleId; }),
                        rules.end());
            Serial.printf("[Automation] 删除规则: %s\n", ruleId.c_str());
        } else {
            rules.clear();
            Serial.println("[Automation] 所有规则已清空");
        }
    }
}

// ----------------- 传感器数据更新 -----------------
void onSensorData(uint8_t slave, uint16_t reg, int value, bool fromAutomation) {
    if (fromAutomation) return;  // 来自自动化控制的状态，不再触发自动化逻辑
    auto it = std::find_if(sensorCache.begin(), sensorCache.end(),
                           [=](const SensorData &d){ return d.slaveId==slave && d.regAddr==reg; });
    if (it != sensorCache.end()) it->value = value;
    else sensorCache.push_back({slave, reg, (float)value});  // 可以用 float 存储

    Serial.printf("[Automation] 更新传感器数据: slave=%d, reg=%d, value=%d\n", slave, reg, value);
}


// ----------------- 规则条件判断 -----------------
bool checkConditions(const AutomationRule &rule) {
    if (!rule.useConditions) return true;

    for (auto &cond : rule.conditions) {
        auto it = std::find_if(sensorCache.begin(), sensorCache.end(),
                               [&](const SensorData &d){ return d.slaveId==cond.slaveId && d.regAddr==cond.regAddr; });
        if (it == sensorCache.end()) {
            Serial.printf("[Automation] 条件检查失败: 未找到传感器数据, slave=%d, reg=%d\n", cond.slaveId, cond.regAddr);
            return false;
        }
        float val = it->value;
        switch (cond.op) {
            case AutomationRule::Condition::GREATER: if (!(val > cond.threshold)) return false; break;
            case AutomationRule::Condition::LESS:    if (!(val < cond.threshold)) return false; break;
            case AutomationRule::Condition::EQUAL:   if (!(val == cond.threshold)) return false; break;
        }
    }
    return true;
}

// ----------------- 时间判断 -----------------
bool checkTimeRange(const AutomationRule &rule) {
    if (!rule.useTimeRange) return true;

    time_t now = time(nullptr);
    struct tm *tm_now = localtime(&now);
    uint8_t wday = tm_now->tm_wday; // 0=Sunday
    wday = (wday==0)?7:wday;         // 转为1-7
    if (!((rule.weekDays >> (wday-1)) & 0x01)) return false;

    int nowMinutes = tm_now->tm_hour*60 + tm_now->tm_min;
    int startMinutes = rule.startHour*60 + rule.startMinute;
    int endMinutes = rule.endHour*60 + rule.endMinute;
    
    if (nowMinutes < startMinutes || nowMinutes > endMinutes) {
        Serial.printf("[Automation] 当前时间: %02d:%02d, 规则: %s, 时间条件不匹配\n", tm_now->tm_hour, tm_now->tm_min, rule.id.c_str());
        return false;
    }
    return true;
}

// ----------------- 自动化任务 -----------------
void taskAutomation(void* pvParameters) {
    Serial.println("[Automation] 自动化任务启动");

    while (true) {
        time_t now = time(nullptr);
        struct tm *tm_now = localtime(&now);
        char timeBuf[20];
        sprintf(timeBuf, "%02d:%02d:%02d", tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);

        Serial.printf("[Automation] 检查规则, 当前时间: %s, 规则数: %d\n", timeBuf, (int)rules.size());

        for (auto &rule : rules) {
            bool timeOk = checkTimeRange(rule);
            bool condOk = checkConditions(rule);

            if (timeOk && condOk) {
                Serial.printf("[Automation] 当前时间: %s, 触发规则: %s\n", timeBuf, rule.id.c_str());
                for (auto &act : rule.actions) {
                    //bool res = ModbusManager::writeCoilAndReport(act.slaveId, act.regAddr, act.sdata);
                    //Serial.printf("[Automation] 执行动作: slave=%d, reg=%d, sdata=%d, 成功=%d\n",act.slaveId, act.regAddr, act.sdata, res);
                }
            } else {
                if (!timeOk) {
                    Serial.printf("[Automation] 当前时间: %s, 规则: %s, 时间条件不匹配\n", timeBuf, rule.id.c_str());
                }
                if (!condOk) {
                    Serial.printf("[Automation] 当前时间: %s, 规则: %s, 条件不匹配\n", timeBuf, rule.id.c_str());
                }
            }
        }
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

// ----------------- 启动任务 -----------------
void startTasks() {
    Serial.println("[Automation] 启动自动化任务");
    xTaskCreatePinnedToCore(taskAutomation, "AutomationTask", 15000, NULL, 1, NULL, 1);
}

// 单次触发规则检查
void triggerOnce() {
    time_t now = time(nullptr);
    struct tm *tm_now = localtime(&now);
    char timeBuf[20];
    sprintf(timeBuf, "%02d:%02d:%02d", tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);

    Serial.printf("[Automation] 单次规则检查, 当前时间: %s, 规则数: %d\n", timeBuf, (int)rules.size());

    for (auto &rule : rules) {
        bool timeOk = checkTimeRange(rule);
        bool condOk = checkConditions(rule);
        if (timeOk && condOk) {
            Serial.printf("[Automation] 当前时间: %s, 触发规则: %s\n", timeBuf, rule.id.c_str());
            for (auto &act : rule.actions) {
                //bool res = ModbusManager::writeCoilAndReport(act.slaveId, act.regAddr, act.sdata);
                //Serial.printf("[Automation] 执行动作: slave=%d, reg=%d, sdata=%d, 成功=%d\n",act.slaveId, act.regAddr, act.sdata, res);
            }
        }
    }
}

} // namespace AutomationManager
