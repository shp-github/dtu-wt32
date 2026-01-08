#include <Arduino.h>
#include "NetManager.h"
#include "TimeManager.h"
#include "MqttSimpleClient.h"
#include "DiscoveryManager.h"
#include "MqttManager.h"
#include "AutomationManager.h"
#include "OtaManager.h"
#include "ResourceManager.h"
#include "NetworkManager.h"
#include "BreathingLED.h"


void setup() {

  Serial.begin(115200);
  Serial.println("\n[MAIN] 系统启动...");

  NetManager::init();      // 初始化网络

  TimeManager::init(8);;  //同步时间

  DiscoveryManager::init(NetManager::clientId);// 初始化设备发现模块

  MqttManager::init();     // 初始化 MQTT
  MqttManager::startTasks(); // 启动 MQTT 任务和心跳任务

  NetworkManager::begin(); // 初始化网络通道管理器

  ConfigManager::begin(); // 先初始化配置

  OtaManager::init();      // 初始化 OTA

  ResourceManager::init(); // 硬件资源上传初始化

  //AutomationManager::init(); //自动化初始化
  //AutomationManager::startTasks();//自动化启动

  // 初始化并启动呼吸灯任务
  BreathingLED::init();
  BreathingLED::startTasks();


  Serial.println("[MAIN] 所有模块已启动");
}

void loop() {
  // 主循环空，逻辑均在 FreeRTOS 任务中执行

}
