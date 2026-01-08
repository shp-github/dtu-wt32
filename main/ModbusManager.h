#pragma once                           // 防止头文件被重复包含
#include <Arduino.h>                   // 引入Arduino核心库
#include <vector>                      // 引入STL动态数组容器

namespace ModbusManager {

  // 数据映射配置结构体
  struct Mapping {
    String k;        // 数据键名（对应JSON中的标识符）
    uint16_t a;  // Modbus寄存器起始地址
    uint16_t l;   // 数据长度（寄存器数量）
    String o;      // 数据字节序（如"big"/"little"）
  };

  // Modbus命令配置结构体
  struct Command {
    uint8_t a;              // 从站设备ID（1-247）
    String f;                 // 功能码（如"01"/"03"/"0F"等）
    uint16_t r;              // 寄存器起始地址
    uint16_t s;             // 读取数据长度
    std::vector<Mapping> arr;  // 数据映射规则数组
  };

  // Modbus主配置结构体
  struct ModbusConfig {
    bool enabled = false;                     // 总开关：是否启用Modbus功能
    String protocol = "rtu";                  // 通信协议类型（rtu/tcp）
    String inputSource = "serial1";           // 数据源接口（串口号）
    std::vector<uint8_t> outputSource;        // 输出目标设备ID数组
    uint16_t interval = 1000;                 // 轮询间隔时间（毫秒）
    std::vector<Command> commands;            // 命令配置列表（动态数组）
  };

  /**
   * @brief 启动Modbus轮询任务
   * 创建并启动FreeRTOS任务，用于周期性轮询从站设备
   */
  void startTasks();

  /**
   * @brief 处理MQTT控制消息
   * @param message JSON格式的控制指令
   * 解析MQTT消息并执行相应的Modbus操作（如写线圈）
   */
  void handleMqttMessage(const String& message);

  /**
   * @brief Modbus轮询任务函数
   * @param pvParameters FreeRTOS任务参数（未使用）
   * 实现定时读取从站数据的核心逻辑
   */
  void taskModbus(void* pvParameters);

  /**
   * @brief 写单个线圈状态
   * @param slaveId 从站ID
   * @param regAddr 线圈寄存器地址
   * @param sdata 写入值（0=OFF，非0=ON）
   * @param maxRetries 最大重试次数（默认3次）
   * @return 是否写入成功
   */
  bool writeCoil(uint8_t slaveId, uint16_t regAddr, int sdata, int maxRetries = 3);

  /**
   * @brief 写线圈并发送MQTT反馈
   * @param slaveId 从站ID
   * @param regAddr 线圈寄存器地址
   * @param sdata 写入值
   * @param maxRetries 最大重试次数
   * @return 是否执行成功
   * 执行写入操作后通过MQTT发送状态反馈
   */
  bool writeCoilAndReport(uint8_t slaveId, uint16_t regAddr, int sdata, int maxRetries = 3);

  /**
   * @brief 应用Modbus配置
   * @param cfg 配置参数对象
   * 初始化Modbus通信参数并保存配置
   */
  void applyConfig(const ModbusConfig &cfg);

  /**
   * @brief 处理读取到的Modbus数据
   * @param cmd 当前执行的命令配置
   * @param isCoil 是否为线圈数据
   * 根据映射规则解析原始数据并转换为JSON格式
   */
  void processModbusData(const ModbusManager::Command &cmd, bool isCoil);

  /**
   * @brief 执行Modbus读取操作
   * @param cmd 要执行的命令配置
   * @param maxRetries 最大重试次数
   * @return 是否读取成功
   * 根据功能码执行对应类型的寄存器读取
   */
  bool executeModbusRead(const ModbusManager::Command &cmd, int maxRetries);

}
