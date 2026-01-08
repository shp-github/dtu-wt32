// CustomChannelAdapter.h
#ifndef CUSTOM_CHANNEL_ADAPTER_H
#define CUSTOM_CHANNEL_ADAPTER_H

#include <Arduino.h>
#include <vector>
#include <functional>

namespace CustomChannelAdapter {

 	// 声明常量（在.cpp中定义）
    extern const int CUSTOM_CHANNEL_INDEX;
    extern const size_t BUFFER_SIZE;

    // 回调函数类型
    typedef std::function<void(const String& message)> MessageCallback;

    // 初始化自定义通道适配器
    void begin();

    // 设置消息接收回调
    void setMessageCallback(MessageCallback callback);

    // 发送数据到自定义通道
    bool publish(const String& topic, const String& payload);

    // 从自定义通道读取消息
    bool readMessage(String& message);

    // 检查是否有消息可用
    bool hasMessageAvailable();

    // 处理接收到的网络数据
    void handleNetworkData(const uint8_t* data, size_t length);

    // 主循环处理
    void loop();

} // namespace CustomChannelAdapter

#endif