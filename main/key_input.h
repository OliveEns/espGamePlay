/**
 * @file key_input.h
 * @brief 按键输入模块 - ADC按键和GPIO返回键处理
 * @author Oliver
 * @date 2026
 */

#ifndef KEY_INPUT_H
#define KEY_INPUT_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ADC通道定义（IO3）
#define ADC_CHANNEL ADC1_CHANNEL_3

// GPIO返回键定义
#define BACK_KEY_GPIO GPIO_NUM_9

// ADC分界线定义
#define KEY_DOWN_THRESHOLD    800
#define KEY_LEFT_THRESHOLD   1600
#define KEY_RIGHT_THRESHOLD  2400
#define KEY_UP_THRESHOLD     3100
#define KEY_OK_THRESHOLD     3900

// 按键定义
typedef enum {
    KEY_NONE = 0,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_UP,
    KEY_OK,
    KEY_BACK
} key_code_t;

/**
 * @brief 初始化按键输入
 * @return ESP_OK 成功，其他失败
 */
esp_err_t key_input_init(void);

/**
 * @brief 获取当前按键状态
 * @return 按键代码
 */
key_code_t key_get(void);

/**
 * @brief 获取按键名称字符串
 * @return 按键名称（"UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK", "NONE"）
 */
const char* key_get_name(key_code_t key);

#ifdef __cplusplus
}
#endif

#endif // KEY_INPUT_H