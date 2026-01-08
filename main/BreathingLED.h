#ifndef BREATHING_LED_H
#define BREATHING_LED_H

class BreathingLED {
public:
    static void init();
    static void startTasks();
    static void enable();
    static void disable();

private:
    static void breathingTask(void* parameter);

    static const int LED_COUNT;
    static const int LED_PINS[2];
    static const int LED_CHANNELS[2];

    static int brightness[2];
    static float angle[2];
    static unsigned long interval;
    static bool enabled;
    static unsigned long previousMillis;
};

#endif
