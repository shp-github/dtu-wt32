#pragma once
#include <Arduino.h>

namespace OtaManager {

    /**
     * @brief 初始化 OTA 更新模块
     * 
     * 设置并启动 OTA 检查任务（FreeRTOS 任务）。此任务会定期检查固件更新，并在检测到新版本时执行下载和升级。
     * 
     * 需要调用此方法以启动 OTA 功能，并定期检查是否有新版本的固件。
     */
    void init();

    /**
     * @brief OTA 检查与升级任务
     * 
     * FreeRTOS 任务函数，周期性向指定服务器检查固件更新：
     * 1. 检查网络是否可用（以太网或 WiFi）。
     * 2. 发送 HTTP 请求获取固件信息。
     * 3. 如果固件存在且有效，则使用 Arduino Update 库执行 OTA 升级。
     * 4. 升级完成后自动重启设备。
     * 
     * @param pvParameters 任务参数（FreeRTOS 使用，可为空）。一般来说此参数无需外部传入。
     */
    void taskOTA(void* pvParameters);

    /**
     * @brief 手动触发 OTA 更新
     * 
     * 该方法可以在需要时通过外部调用触发 OTA 更新。
     * 提供固件下载的 URL，执行下载和升级。
     * 
     * @param firmware_url 固件的下载 URL。必须是指向有效固件文件的 URL 地址。
     */
    void startOTAUpdate(const char* firmware_url);

    /**
     * @brief 下载并更新固件
     * 
     * 该方法用于下载指定 URL 的固件并执行 OTA 更新。下载完成后，会将固件写入设备内存并验证是否成功。
     * 如果固件下载和写入过程顺利，设备会自动重启。
     * 
     * @param firmware_url 固件下载 URL。此 URL 应指向可用的固件文件。
     * @return true 如果更新成功，设备成功重启。
     * @return false 如果更新失败，固件下载或写入失败。
     */
    bool downloadAndUpdateFirmware(const char* firmware_url);

    /**
     * @brief 检查并更新固件
     * 
     * 该方法用于检查固件更新，并触发下载更新过程。它会发送 HTTP 请求检查是否有新版本的固件，
     * 如果有则调用下载并更新固件的过程。
     * 
     * 通过调用此方法，设备会定期检查更新，并在有新版本时自动更新。
     */
    void checkAndUpdateFirmware();

}
