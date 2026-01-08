#pragma once
#include <Arduino.h>

namespace TimeManager {
    void init(int tzOffset = 8 * 3600);   // 初始化时间同步，默认东八区
    bool isTimeValid();                   // 检查系统时间是否有效
    void syncNow();                       // 手动触发同步
}
