#include "BreathingLED.h"
#include <Arduino.h>

// ========= 静态成员定义 =========
const int BreathingLED::LED_COUNT = 2;
const int BreathingLED::LED_PINS[2] = {15, 14};            // 只用 15 和 14
const int BreathingLED::LED_CHANNELS[2] = {1, 2};          // PWM 通道

int BreathingLED::brightness[2] = {0, 0};
float BreathingLED::angle[2] = {0, PI};                    // 180° 相反 → 交替效果
unsigned long BreathingLED::interval = 30;
bool BreathingLED::enabled = true;
unsigned long BreathingLED::previousMillis = 0;



// ==================== 初始化 ====================
void BreathingLED::init() {
    Serial.println("[BreathingLED] 初始化(2灯交替)...");

    for (int i = 0; i < LED_COUNT; i++) {
        ledcSetup(LED_CHANNELS[i], 5000, 8);
        ledcAttachPin(LED_PINS[i], LED_CHANNELS[i]);
        ledcWrite(LED_CHANNELS[i], 0);

        Serial.printf("[BreathingLED] GPIO %d -> CH%d\n",
                      LED_PINS[i], LED_CHANNELS[i]);
    }
    Serial.println("[BreathingLED] 初始化完成");
}



// ==================== 创建任务 ====================
void BreathingLED::startTasks() {
    xTaskCreatePinnedToCore(
        breathingTask,
        "BreathingLED",
        4096,
        NULL,
        1,
        NULL,
        1
    );
}



// ==================== 任务（交替呼吸） ====================
void BreathingLED::breathingTask(void* parameter) {
    while (true) {
        unsigned long now = millis();

        if (enabled && now - previousMillis >= interval) {
            previousMillis = now;

            for (int i = 0; i < LED_COUNT; i++) {
                angle[i] += 0.05;
                if (angle[i] >= 2 * PI) angle[i] = 0;

                // 正弦波呼吸
                brightness[i] = (sin(angle[i]) + 1) * 127.5; // 0~255
                ledcWrite(LED_CHANNELS[i], brightness[i]);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}



// ==================== 控制 ====================
void BreathingLED::enable() {
    enabled = true;
}

void BreathingLED::disable() {
    enabled = false;
    for (int i = 0; i < LED_COUNT; i++)
        ledcWrite(LED_CHANNELS[i], 0);
}
