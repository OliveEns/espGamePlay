/**
 * @file main.c
 * @brief ESP32-C3 游戏控制台主程序（完整测试版）
 * @author Oliver
 * @date 2024
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "st7789.h"
#include "st7789_data.h"
#include "key_input.h"
#include "game_manager.h"
#include "lua_binding.h"
#include "game_task.h"
#include "download_manager.h"

static const char *TAG = "GAME_CONSOLE";

// 最大游戏数量
#define MAX_GAMES 32

// 菜单项偏移（游戏列表之后增加两个下载选项）
#define MENU_OFFSET_SERIAL_DOWNLOAD  0
#define MENU_OFFSET_WIFI_DOWNLOAD    1

/**
 * @brief 创建测试游戏（如果不存在）
 * @return true 表示创建了新的测试游戏，false 表示已存在或创建失败
 */
static bool create_test_game_if_needed(void) {
    game_info_t games[MAX_GAMES];
    int game_count = 0;
    esp_err_t ret = game_list_scan(games, MAX_GAMES, &game_count);
    if (ret == ESP_OK && game_count > 0) {
        ESP_LOGI(TAG, "已有 %d 个游戏，跳过创建测试游戏", game_count);
        return false;
    }

    const char *test_lua_script =
        "-- 移动方块测试游戏\n"
        "local x, y = 100, 100\n"
        "local dx, dy = 2, 2\n"
        "local size = 20\n"
        "local color = Game.rgb(255, 255, 0)\n"
        "\n"
        "function on_init()\n"
        "    Game.clear_screen(Game.rgb(0, 0, 0))\n"
        "    print('游戏初始化完成')\n"
        "end\n"
        "\n"
        "function on_update(dt)\n"
        "    x = x + dx\n"
        "    y = y + dy\n"
        "    if x < 0 or x > 240 - size then\n"
        "        dx = -dx\n"
        "    end\n"
        "    if y < 0 or y > 240 - size then\n"
        "        dy = -dy\n"
        "    end\n"
        "end\n"
        "\n"
        "function on_render()\n"
        "    Game.clear_screen(Game.rgb(0, 0, 0))\n"
        "    Game.draw_rect(x, y, size, size, color)\n"
        "    Game.draw_text(0, 0, '按BACK返回菜单', Game.rgb(255, 255, 255))\n"
        "end\n";

    game_header_t header = {
        .magic = {'G', 'M', '0', '1'},
        .version = GAME_FORMAT_VERSION,
        .reserved = 0,
        .game_name_len = 4,
        .game_name = "test",
        .author = "Oliver",
        .script_crc = 0,
        .script_size = 0
    };

    ret = game_create(&header, (const uint8_t *)test_lua_script, strlen(test_lua_script));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建测试游戏失败: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "测试游戏创建成功");
    return true;
}

/**
 * @brief 显示主菜单界面
 * @param games 游戏列表
 * @param game_count 游戏数量
 * @param selected 当前选中的索引（0 表示第一个游戏，game_count 表示串口下载，game_count+1 表示WiFi下载）
 */
static void display_menu(game_info_t *games, int game_count, int selected) {
    st7789_clear(COLOR_BLACK);
    st7789_draw_string(0, 0, "=== Game Menu ===", COLOR_YELLOW, COLOR_BLACK);

    // 显示游戏列表
    for (int i = 0; i < game_count; i++) {
        uint16_t color = (i == selected) ? COLOR_GREEN : COLOR_WHITE;
        st7789_draw_string(0, 20 + i * 16, games[i].name, color, COLOR_BLACK);
    }

    // 显示串口下载选项
    int serial_idx = game_count;
    uint16_t serial_color = (serial_idx == selected) ? COLOR_GREEN : COLOR_CYAN;
    st7789_draw_string(0, 20 + serial_idx * 16, "S: Serial Download", serial_color, COLOR_BLACK);

    // 显示 WiFi 下载选项
    int wifi_idx = game_count + 1;
    uint16_t wifi_color = (wifi_idx == selected) ? COLOR_GREEN : COLOR_CYAN;
    st7789_draw_string(0, 20 + wifi_idx * 16, "W: WiFi Download", wifi_color, COLOR_BLACK);

    // 提示信息
    st7789_draw_string(0, 20 + (game_count + 2) * 16, "UP/DOWN: Select", COLOR_GRAY, COLOR_BLACK);
    st7789_draw_string(0, 20 + (game_count + 3) * 16, "OK: Enter  BACK: Exit", COLOR_GRAY, COLOR_BLACK);
}

/**
 * @brief 显示下载等待界面
 * @param mode 0:串口, 1:WiFi
 * @param ssid WiFi SSID（仅WiFi模式）
 * @param password WiFi密码（仅WiFi模式）
 * @param port 端口号
 */
static void show_download_screen(int mode, const char *ssid, const char *password, uint16_t port) {
    st7789_clear(COLOR_BLACK);
    if (mode == 0) {
        st7789_draw_string(0, 0, "Serial Download Mode", COLOR_WHITE, COLOR_BLACK);
        st7789_draw_string(0, 20, "Please send .game file", COLOR_GRAY, COLOR_BLACK);
        st7789_draw_string(0, 40, "via serial (115200 baud)", COLOR_GRAY, COLOR_BLACK);
    } else {
        st7789_draw_string(0, 0, "WiFi Download Mode", COLOR_WHITE, COLOR_BLACK);
        char buf[64];
        snprintf(buf, sizeof(buf), "SSID: %s", ssid);
        st7789_draw_string(0, 20, buf, COLOR_GRAY, COLOR_BLACK);
        snprintf(buf, sizeof(buf), "Password: %s", password);
        st7789_draw_string(0, 36, buf, COLOR_GRAY, COLOR_BLACK);
        snprintf(buf, sizeof(buf), "TCP Port: %d", port);
        st7789_draw_string(0, 52, buf, COLOR_GRAY, COLOR_BLACK);
        st7789_draw_string(0, 68, "Connect to AP and send", COLOR_GRAY, COLOR_BLACK);
        st7789_draw_string(0, 84, ".game file via TCP", COLOR_GRAY, COLOR_BLACK);
    }
    st7789_draw_string(0, 120, "Waiting...", COLOR_YELLOW, COLOR_BLACK);
    st7789_draw_string(0, 200, "Press BACK to cancel", COLOR_RED, COLOR_BLACK);
}

/**
 * @brief 主菜单任务
 */
static void main_menu_task(void *arg) {
    esp_err_t ret;
    game_info_t games[MAX_GAMES];
    int game_count = 0;
    int selected = 0;   // 当前选中的索引

    // 初始化屏幕
    st7789_init();
    st7789_clear(COLOR_BLACK);
    st7789_draw_string(0, 0, "System Booting...", COLOR_WHITE, COLOR_BLACK);

    // 初始化按键
    ret = key_input_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(ret));
        st7789_draw_string(0, 20, "Key init failed!", COLOR_RED, COLOR_BLACK);
        vTaskDelay(pdMS_TO_TICKS(3000));
        vTaskDelete(NULL);
        return;
    }

    // 初始化文件系统
    ret = game_fs_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "文件系统初始化失败: %s", esp_err_to_name(ret));
        st7789_draw_string(0, 20, "FS init failed!", COLOR_RED, COLOR_BLACK);
        vTaskDelay(pdMS_TO_TICKS(3000));
        vTaskDelete(NULL);
        return;
    }

    // 创建测试游戏（如果不存在）
    create_test_game_if_needed();

    // 主循环
    while (1) {
        // 刷新游戏列表
        game_count = 0;
        ret = game_list_scan(games, MAX_GAMES, &game_count);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "扫描游戏列表失败: %s", esp_err_to_name(ret));
            game_count = 0;
        }

        // 显示菜单
        display_menu(games, game_count, selected);

        // 等待有效按键
        key_code_t key;
        do {
            key = key_get();
            vTaskDelay(pdMS_TO_TICKS(50));
        } while (key == KEY_NONE);

        // 处理按键
        switch (key) {
            case KEY_UP:
                if (selected > 0) selected--;
                else selected = game_count + 1;  // 循环到底部
                break;
            case KEY_DOWN:
                if (selected < game_count + 1) selected++;
                else selected = 0;               // 循环到顶部
                break;
            case KEY_OK:
                if (selected < game_count) {
                    // 启动游戏
                    ESP_LOGI(TAG, "启动游戏: %s", games[selected].name);
                    st7789_clear(COLOR_BLACK);
                    st7789_draw_string(0, 0, "Loading...", COLOR_WHITE, COLOR_BLACK);
                    ret = game_run(games[selected].filename);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "启动游戏失败: %s", esp_err_to_name(ret));
                        st7789_draw_string(0, 20, "Game start failed!", COLOR_RED, COLOR_BLACK);
                        vTaskDelay(pdMS_TO_TICKS(2000));
                    } else {
                        // 等待游戏结束
                        while (game_task_get_handle() != NULL) {
                            vTaskDelay(pdMS_TO_TICKS(100));
                        }
                        st7789_clear(COLOR_BLACK);
                        st7789_draw_string(0, 0, "Game exited", COLOR_WHITE, COLOR_BLACK);
                        vTaskDelay(pdMS_TO_TICKS(1000));
                    }
                } else if (selected == game_count) {
                    // 串口下载
                    ESP_LOGI(TAG, "启动串口下载模式");
                    show_download_screen(0, NULL, NULL, 0);
                    download_start_serial(UART_NUM_0, 115200);
                    while (download_is_active()) {
                        // 显示动态等待效果
                        static int dot = 0;
                        st7789_draw_string(0, 140, "Receiving...", COLOR_CYAN, COLOR_BLACK);
                        char dots[4] = {0, 0, 0, 0};
                        dots[0] = '.';
                        if (dot % 10 < 3) dots[0] = '.';
                        else if (dot % 10 < 6) dots[1] = '.';
                        else dots[2] = '.';
                        st7789_draw_string(0, 160, dots, COLOR_CYAN, COLOR_BLACK);
                        dot++;
                        vTaskDelay(pdMS_TO_TICKS(200));

                        // 检查返回键退出下载
                        if (key_get() == KEY_BACK) {
                            download_stop();
                            break;
                        }
                    }
                    ESP_LOGI(TAG, "串口下载结束");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                } else if (selected == game_count + 1) {
                    // WiFi 下载
                    ESP_LOGI(TAG, "启动WiFi下载模式");
                    const char *ssid = "ESP_Game_AP";
                    const char *pwd = "12345678";
                    uint16_t port = 8888;
                    show_download_screen(1, ssid, pwd, port);
                    download_start_wifi(ssid, pwd, port);
                    while (download_is_active()) {
                        // 动态等待效果
                        static int dot = 0;
                        st7789_draw_string(0, 140, "Waiting for connection...", COLOR_CYAN, COLOR_BLACK);
                        char dots[4] = {'.', '.', '.', 0};
                        dots[0] = '.';
                        if (dot % 10 < 3) dots[0] = '.';
                        else if (dot % 10 < 6) dots[1] = '.';
                        else dots[2] = '.';
                        st7789_draw_string(0, 160, dots, COLOR_CYAN, COLOR_BLACK);
                        dot++;
                        vTaskDelay(pdMS_TO_TICKS(200));

                        if (key_get() == KEY_BACK) {
                            download_stop();
                            break;
                        }
                    }
                    ESP_LOGI(TAG, "WiFi下载结束");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                break;
            case KEY_BACK:
                // 在主菜单按返回键无操作（或可添加关机/待机功能）
                ESP_LOGI(TAG, "BACK key pressed in menu");
                break;
            default:
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // 消抖
    }
}

/**
 * @brief 应用程序入口
 */
void app_main(void) {
    ESP_LOGI(TAG, "游戏控制台启动...");

    // 初始化 NVS（WiFi 和部分系统组件需要）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 创建主菜单任务（栈大小 8192，优先级 2）
    xTaskCreate(main_menu_task, "main_menu", 16384, NULL, 2, NULL);
}