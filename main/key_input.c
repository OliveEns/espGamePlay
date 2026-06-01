/**
 * @file key_input.c
 * @brief 按键输入模块实现 - ADC按键和GPIO返回键处理
 * @author Oliver
 * @date 2026
 */

#include "key_input.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "KEY_INPUT";

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

// 按键名称映射
static const char* key_names[] = {
    "NONE",
    "DOWN",
    "LEFT",
    "RIGHT",
    "UP",
    "OK",
    "BACK"
};

/**
 * @brief 获取按键名称字符串
 */
const char* key_get_name(key_code_t key) {
    if (key >= KEY_NONE && key <= KEY_BACK) {
        return key_names[key];
    }
    return "NONE";
}

/**
 * @brief 获取当前按键状态
 */
key_code_t key_get(void) {
    // 先检查返回键（GPIO9，低电平有效）
    if (gpio_get_level(BACK_KEY_GPIO) == 0) {
        return KEY_BACK;
    }
    
    // 读取ADC值
    int adc_value = adc1_get_raw(ADC_CHANNEL);
    if (adc_value < KEY_OK_THRESHOLD){
        ESP_LOGI(TAG, "当前ADC值: %d", adc_value);
    }
    
    // 根据分界线判断按键
    if (adc_value < KEY_DOWN_THRESHOLD) {
        return KEY_DOWN;
    } else if (adc_value < KEY_LEFT_THRESHOLD) {
        return KEY_LEFT;
    } else if (adc_value < KEY_RIGHT_THRESHOLD) {
        return KEY_RIGHT;
    } else if (adc_value < KEY_UP_THRESHOLD) {
        return KEY_UP;
    } else if (adc_value < KEY_OK_THRESHOLD) {
        return KEY_OK;
    } else {
        return KEY_NONE;
    }
}

/**
 * @brief 初始化按键输入
 */
esp_err_t key_input_init(void) {
    ESP_LOGI(TAG, "初始化按键输入...");
    
    // 初始化ADC
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_11);
    
    // 初始化GPIO返回键（上拉输入，低电平有效）
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BACK_KEY_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    ESP_LOGI(TAG, "按键输入初始化完成");
    return ESP_OK;
}