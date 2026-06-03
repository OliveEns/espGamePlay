/**
 * @file lua_binding.h
 * @brief Lua 硬件绑定层 - 提供游戏脚本调用的硬件接口
 * @author Oliver
 * @date 2026
 *
 * 该模块在 Lua 中注册一个全局的 "Game" 表，包含以下函数：
 * - Game.clear_screen(color)           清屏
 * - Game.draw_pixel(x, y, color)       画点
 * - Game.draw_rect(x, y, w, h, color)  填充矩形
 * - Game.draw_text(x, y, text, color)  显示字符串（背景黑色）
 * - Game.get_key()                     获取按键名称（字符串）
 * - Game.update_display()              刷新显示（当前为空操作）
 * - Game.get_tick()                    获取系统毫秒时间戳
 * - Game.rgb(r, g, b)                  将RGB(0-255)转换为RGB565颜色值
 * - Game.draw_line(x1,y1,x2,y2,color)  绘制直线
 * - Game.draw_rect_border(x,y,w,h,color) 绘制矩形边框
 * - Game.draw_circle(x0,y0,r,color)    绘制圆形边框
 * - Game.fill_circle(x0,y0,r,color)    填充圆形
 * - Game.draw_triangle(x1,y1,x2,y2,x3,y3,color) 绘制三角形边框
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