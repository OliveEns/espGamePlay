/**
 * @file download_manager.h
 * @brief 下载管理器 - 游戏下载任务处理
 * @author Oliver
 * @date 2026
 */

#ifndef DOWNLOAD_MANAGER_H
#define DOWNLOAD_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 串口下载模式
 * @param uart_num UART端口号（如 UART_NUM_0）
 * @param baud_rate 波特率
 */
void download_start_serial(int uart_num, int baud_rate);

/**
 * @brief WiFi 下载模式（ESP32 作为 SoftAP，TCP 服务器）
 * @param ssid 热点 SSID，默认 "ESP_Game_AP"
 * @param password 密码，默认 "12345678"
 * @param port TCP 端口，默认 8888
 */
void download_start_wifi(const char *ssid, const char *password, uint16_t port);

/**
 * @brief 停止当前下载
 */
void download_stop(void);

/**
 * @brief 检查是否正在下载
 */
bool download_is_active(void);

#ifdef __cplusplus
}
#endif

#endif