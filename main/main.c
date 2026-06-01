/**
 * @file main.c
 * @brief ESP32-C3 游戏控制台主程序
 * @author Oliver
 * @date 2026
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "game_manager.h"
#include "lua_binding.h"
#include "game_task.h"
#include "key_input.h"
#include "st7789.h"
#include "st7789_data.h"

static const char *TAG = "GAME_CONSOLE";

/**
 * @brief 创建测试游戏
 */
static void create_test_game(void) {
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
    
    // 注意：游戏名称改为 "test"
    game_header_t header = {
        .magic = {'G', 'M', '0', '1'},
        .version = GAME_FORMAT_VERSION,
        .reserved = 0,
        .game_name_len = 4,
        .game_name = "test",      // 改为 "test"
        .author = "Oliver",
        .script_crc = 0,
        .script_size = 0
    };
    
    esp_err_t ret = game_create(&header, (const uint8_t *)test_lua_script, strlen(test_lua_script));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建测试游戏失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "测试游戏创建成功");
        // 可选：删除旧的 HelloGame
        game_delete("HelloGame");
    }
}

/**
 * @brief 主菜单任务
 */
void main_menu_task(void *arg) {
    esp_err_t ret;
    game_info_t games[MAX_GAMES];
    int game_count = 0;
    int selected_index = 0;
    
    ESP_LOGI(TAG, "主菜单任务启动...");
    
    // 初始化屏幕
    st7789_init();
    st7789_clear(COLOR_BLACK);
    
    // 初始化按键输入
    ret = key_input_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    // 初始化文件系统
    ret = game_fs_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "文件系统初始化失败: %s", esp_err_to_name(ret));
        st7789_draw_string(0, 0, "文件系统初始化失败!", COLOR_RED, COLOR_BLACK);
        vTaskDelete(NULL);
        return;
    }

    // 强制删除旧的 HelloGame（避免残留）
    game_delete("HelloGame");
    ESP_LOGI(TAG, "已删除旧游戏 HelloGame");

    // 检查 test 游戏是否存在，如果不存在则创建
    game_count = 0;
    ret = game_list_scan(games, MAX_GAMES, &game_count);
    if (ret == ESP_OK && game_count == 0) {
        ESP_LOGI(TAG, "创建测试游戏...");
        create_test_game();
    } else {
        ESP_LOGI(TAG, "已有 %d 个游戏，跳过创建", game_count);
    }
    
    while (1) {
        // 扫描游戏列表
        game_count = 0;
        ret = game_list_scan(games, MAX_GAMES, &game_count);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "扫描游戏列表失败: %s", esp_err_to_name(ret));
            game_count = 0;
        }
        
        // 显示菜单
        st7789_clear(COLOR_BLACK);
        st7789_draw_string(0, 0, "=== Game Menu ===", COLOR_YELLOW, COLOR_BLACK);
        
        if (game_count == 0) {
            st7789_draw_string(0, 20, "No Games", COLOR_WHITE, COLOR_BLACK);
            st7789_draw_string(0, 40, "Press OK to refresh", COLOR_GRAY, COLOR_BLACK);
        } else {
            for (int i = 0; i < game_count; i++) {
                uint16_t color = (i == selected_index) ? COLOR_GREEN : COLOR_WHITE;
                st7789_draw_string(0, 20 + i * 16, games[i].name, color, COLOR_BLACK);
            }
            st7789_draw_string(0, 20 + game_count * 16, "Press OK to start playing", COLOR_GRAY, COLOR_BLACK);
        }
        
        // 等待按键
        key_code_t key;
        do {
            key = key_get();
            vTaskDelay(pdMS_TO_TICKS(50));
        } while (key == KEY_NONE);
        
        // 处理按键
        if (game_count > 0) {
            switch (key) {
                case KEY_UP:
                    ESP_LOGI(TAG, "检测到按键UP");
                    if (selected_index > 0) {
                        selected_index--;
                    }
                    break;
                case KEY_DOWN:
                    ESP_LOGI(TAG, "检测到按键DOWN");
                    if (selected_index < game_count - 1) {
                        selected_index++;
                    }
                    break;
                case KEY_OK:
                    ESP_LOGI(TAG, "检测到按键OK");
                    // 启动游戏
                    ESP_LOGI(TAG, "启动游戏: %s", games[selected_index].name);
                    st7789_clear(COLOR_BLACK);
                    st7789_draw_string(0, 0, "Loading...", COLOR_WHITE, COLOR_BLACK);
                    
                    ret = game_run(games[selected_index].name);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "启动游戏失败: %s", esp_err_to_name(ret));
                        st7789_draw_string(0, 20, "Fail to start game!", COLOR_RED, COLOR_BLACK);
                        vTaskDelay(pdMS_TO_TICKS(2000));
                    } else {
                        // 等待游戏结束（通过返回键退出）
                        while (game_task_get_handle() != NULL) {
                            vTaskDelay(pdMS_TO_TICKS(100));
                        }
                        st7789_clear(COLOR_BLACK);
                        st7789_draw_string(0, 0, "Exited", COLOR_WHITE, COLOR_BLACK);
                        vTaskDelay(pdMS_TO_TICKS(1000));
                    }
                    break;
                default:
                    break;
            }
        }
        
        // 延时消抖
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "游戏控制台启动...");
    
    // 创建主菜单任务
    xTaskCreate(main_menu_task, "main_menu", 8192, NULL, tskIDLE_PRIORITY + 2, NULL);
}