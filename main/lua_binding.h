/**
 * @file lua_binding.h
 * @brief Lua 硬件绑定层 - 提供游戏脚本调用的硬件接口
 * @author Oliver
 * @date 2026
 */

#ifndef LUA_BINDING_H
#define LUA_BINDING_H

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册 Game 模块到 Lua 状态机
 * @param L Lua 状态机
 */
void lua_binding_register_game(lua_State *L);

/**
 * @brief 更新按键状态（供游戏任务调用）
 */
void lua_binding_update_keys(void);

#ifdef __cplusplus
}
#endif

#endif // LUA_BINDING_H