#pragma once
#include <Arduino.h>

namespace MqttManager {

  /**
   * @brief 初始化 MQTT 客户端
   * 
   * 配置服务器地址、端口、回调函数等，为 MQTT 连接做好准备。
   */
  void init();

  /**
   * @brief 启动 MQTT 相关的 FreeRTOS 任务
   * 
   * 启动两个任务：
   * 1. MQTT 主循环任务（处理连接和消息循环）
   * 2. 心跳任务（定期发送心跳到服务器）
   */
  void startTasks();

  /**
   * @brief 发布消息到指定 MQTT 主题
   * 
   * @param topic 发布的主题
   * @param payload 发布的消息内容
   * 
   * 注意：只有在 MQTT 已连接时才能发布成功。
   */
  void publish(const String& topic, const String& payload);

  /**
   * @brief MQTT 主循环任务
   * 
   * FreeRTOS 任务函数，负责保持与 MQTT 服务器的连接，
   * 并处理收到的消息回调。
   * @param pvParameters 任务参数（FreeRTOS 使用）
   */
  void taskMQTT(void* pvParameters);

  /**
   * @brief 心跳任务
   * 
   * FreeRTOS 任务函数，定期向服务器发送心跳包，
   * 用于让服务器知道设备在线。
   * @param pvParameters 任务参数（FreeRTOS 使用）
   */
  void taskHeartbeat(void* pvParameters);

  /**
   * @brief 处理通过 MQTT 接收到的固件更新请求
   * 
   * 解析固件 URL 和 MD5，并触发 OTA 更新操作。
   * 
   * @param message MQTT 消息内容（包含固件 URL 和 MD5）
   */
  void handleUpdateRequest(const String& message);




}  // namespace MqttManager
