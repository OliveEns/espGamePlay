/**
 * @file game_task.c
 * @brief 游戏任务模块实现
 * @author Oliver
 * @date 2026
 */

#include <string.h> 
#include "game_task.h"
#include "game_manager.h"
#include "lua_binding.h"
#include "key_input.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "GAME_TASK";

// 游戏任务参数结构体
typedef struct {
    lua_State *L;
    const char *game_name;
} game_task_params_t;

// 全局变量
static TaskHandle_t game_task_handle = NULL;
static volatile bool game_task_exit_flag = false;

/**
 * @brief 游戏任务入口函数
 */
static void game_task_entry(void *params) {
    esp_task_wdt_add(NULL);
    game_task_params_t *p = (game_task_params_t *)params;
    lua_State *L = p->L;
    const char *game_name = p->game_name;
    
    ESP_LOGI(TAG, "游戏任务启动: %s", game_name);
    
    uint64_t last_tick = esp_timer_get_time() / 1000;
    game_task_exit_flag = false;
    
    while (!game_task_exit_flag) {
        // 重置看门狗
        esp_task_wdt_reset();
        
        // 计算帧间隔时间 dt（秒）
        uint64_t current_tick = esp_timer_get_time() / 1000;
        float dt = (current_tick - last_tick) / 1000.0f;
        last_tick = current_tick;
        
        // 更新按键状态
        lua_binding_update_keys();
        
        // 检查返回键（GPIO9）
        if (key_get() == KEY_BACK) {
            ESP_LOGI(TAG, "检测到返回键，退出游戏");
            game_task_exit_flag = true;
            break;
        }
        
        // 调用 on_update(dt)
        lua_getglobal(L, "on_update");
        lua_pushnumber(L, dt);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            ESP_LOGE(TAG, "调用 on_update 失败: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        
        // 调用 on_render()
        lua_getglobal(L, "on_render");
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            ESP_LOGE(TAG, "调用 on_render 失败: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        
        // 更新显示
        lua_getglobal(L, "Game");
        lua_getfield(L, -1, "update_display");
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            ESP_LOGE(TAG, "调用 update_display 失败: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 1); // 弹出 Game 表
        
        // 约30 FPS
        vTaskDelay(pdMS_TO_TICKS(33));
    }
    
    ESP_LOGI(TAG, "游戏任务结束: %s", game_name);
    game_on_task_exit();
    free((void *)p->game_name);
    free(p);
    
    // 通知等待任务结束
    game_task_handle = NULL;
    esp_task_wdt_delete(NULL); 
    vTaskDelete(NULL);
}

/**
 * @brief 创建游戏任务
 */
void game_task_create(lua_State *L, const char *game_name) {
    ESP_LOGI(TAG, "创建游戏任务: %s", game_name);
    
    // 分配任务参数
    game_task_params_t *params = (game_task_params_t *)malloc(sizeof(game_task_params_t));
    params->L = L;
    params->game_name = strdup(game_name);
    
    // 创建任务（8192字节栈，低优先级）
    xTaskCreate(
        game_task_entry,
        "game_task",
        8192,
        params,
        tskIDLE_PRIORITY + 1,
        &game_task_handle
    );
    
    ESP_LOGI(TAG, "游戏任务创建成功");
}

/**
 * @brief 获取当前游戏任务句柄
 */
TaskHandle_t game_task_get_handle(void) {
    return game_task_handle;
}

/**
 * @brief 请求游戏任务退出
 */
void game_task_request_exit(void) {
    ESP_LOGI(TAG, "请求游戏任务退出");
    game_task_exit_flag = true;
}

/**
 * @brief 等待游戏任务结束
 */
void game_task_wait_exit(void) {
    if (game_task_handle != NULL) {
        ESP_LOGI(TAG, "等待游戏任务结束...");
        while (game_task_handle != NULL) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        ESP_LOGI(TAG, "游戏任务已结束");
    }
}