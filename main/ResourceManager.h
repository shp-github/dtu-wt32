#pragma once
#include <Arduino.h>

namespace ResourceManager {

  /**
   * @brief 初始化资源监控模块
   * - 初始化 SPIFFS
   * - 注册 CPU 空闲钩子
   * - 启动定时上报任务
   */
  void init();

  /**
   * @brief 收集硬件资源并上传 MQTT
   * - 包含 heap、PSRAM、flash、CPU、网络等信息
   */
  void collectAndUpload();

  /**
   * @brief 后台任务，周期性调用 collectAndUpload()
   */
  void taskResource(void* pvParameters);

  /**
   * @brief 获取 CPU 当前使用率（百分比）
   * @return float [0~100]
   */
  float getCpuUsage();

} // namespace ResourceManager
