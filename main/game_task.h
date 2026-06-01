/**
 * @file game_task.h
 * @brief 游戏任务模块 - 管理Lua游戏脚本的执行循环
 * @author Oliver
 * @date 2026
 */

#ifndef GAME_TASK_H
#define GAME_TASK_H

#include "lua.h"
#include "game_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建游戏任务
 * @param L Lua状态机
 * @param game_name 游戏名称
 */
void game_task_create(lua_State *L, const char *game_name);

/**
 * @brief 获取当前游戏任务句柄
 * @return 任务句柄，如果没有运行中的游戏返回 NULL
 */
TaskHandle_t game_task_get_handle(void);

/**
 * @brief 请求游戏任务退出
 */
void game_task_request_exit(void);

/**
 * @brief 等待游戏任务结束
 */
void game_task_wait_exit(void);

#ifdef __cplusplus
}
#endif

#endif // GAME_TASK_H