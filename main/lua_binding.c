/**
 * @file lua_binding.c
 * @brief Lua 硬件绑定层实现
 * @author Oliver
 * @date 2026
 */

#include "lua_binding.h"
#include "lauxlib.h"
#include "lualib.h"
#include "st7789.h"
#include "st7789_data.h"
#include "key_input.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "LUA_BINDING";

// 当前按键状态（全局变量，供 Lua get_key 使用）
static key_code_t current_key = KEY_NONE;

/**
 * @brief Game.clear_screen(color) - 清屏
 */
static int lua_game_clear_screen(lua_State *L) {
    uint16_t color = (uint16_t)luaL_checkinteger(L, 1);
    st7789_clear(color);
    return 0;
}

/**
 * @brief Game.draw_pixel(x, y, color) - 画像素
 */
static int lua_game_draw_pixel(lua_State *L) {
    uint16_t x = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t y = (uint16_t)luaL_checkinteger(L, 2);
    uint16_t color = (uint16_t)luaL_checkinteger(L, 3);
    st7789_draw_pixel(x, y, color);
    return 0;
}

/**
 * @brief Game.draw_rect(x, y, w, h, color) - 画填充矩形
 */
static int lua_game_draw_rect(lua_State *L) {
    uint16_t x = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t y = (uint16_t)luaL_checkinteger(L, 2);
    uint16_t w = (uint16_t)luaL_checkinteger(L, 3);
    uint16_t h = (uint16_t)luaL_checkinteger(L, 4);
    uint16_t color = (uint16_t)luaL_checkinteger(L, 5);
    st7789_fill_rect(x, y, x + w - 1, y + h - 1, color);
    return 0;
}

/**
 * @brief Game.draw_text(x, y, text, color) - 显示文字
 */
static int lua_game_draw_text(lua_State *L) {
    uint16_t x = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t y = (uint16_t)luaL_checkinteger(L, 2);
    const char *text = luaL_checkstring(L, 3);
    uint16_t color = (uint16_t)luaL_checkinteger(L, 4);
    st7789_draw_string(x, y, text, color, COLOR_BLACK);
    return 0;
}

/**
 * @brief Game.get_key() - 获取按键
 */
static int lua_game_get_key(lua_State *L) {
    lua_pushstring(L, key_get_name(current_key));
    return 1;
}

/**
 * @brief Game.update_display() - 更新显示
 */
static int lua_game_update_display(lua_State *L) {
    // ST7789直接操作屏幕，无需额外刷新
    return 0;
}

/**
 * @brief Game.get_tick() - 获取毫秒时间戳
 */
static int lua_game_get_tick(lua_State *L) {
    uint64_t tick = esp_timer_get_time() / 1000;
    lua_pushnumber(L, (lua_Number)tick);
    return 1;
}

/**
 * @brief Game.rgb(r, g, b) - RGB转RGB565颜色
 */
static int lua_game_rgb(lua_State *L) {
    int r = luaL_checkinteger(L, 1);
    int g = luaL_checkinteger(L, 2);
    int b = luaL_checkinteger(L, 3);
    uint16_t color = RGB565(r, g, b);
    lua_pushinteger(L, color);
    return 1;
}

/**
 * @brief Game.draw_line(x1, y1, x2, y2, color) - 绘制直线
 */
static int lua_game_draw_line(lua_State *L) {
    uint16_t x1 = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t y1 = (uint16_t)luaL_checkinteger(L, 2);
    uint16_t x2 = (uint16_t)luaL_checkinteger(L, 3);
    uint16_t y2 = (uint16_t)luaL_checkinteger(L, 4);
    uint16_t color = (uint16_t)luaL_checkinteger(L, 5);
    st7789_draw_line(x1, y1, x2, y2, color);
    return 0;
}

/**
 * @brief Game.draw_rect_border(x, y, w, h, color) - 绘制矩形边框
 */
static int lua_game_draw_rect_border(lua_State *L) {
    uint16_t x = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t y = (uint16_t)luaL_checkinteger(L, 2);
    uint16_t w = (uint16_t)luaL_checkinteger(L, 3);
    uint16_t h = (uint16_t)luaL_checkinteger(L, 4);
    uint16_t color = (uint16_t)luaL_checkinteger(L, 5);
    st7789_draw_rect_border(x, y, x + w - 1, y + h - 1, color);
    return 0;
}

/**
 * @brief Game.draw_circle(x0, y0, r, color) - 绘制圆形边框
 */
static int lua_game_draw_circle(lua_State *L) {
    uint16_t x0 = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t y0 = (uint16_t)luaL_checkinteger(L, 2);
    uint16_t r  = (uint16_t)luaL_checkinteger(L, 3);
    uint16_t color = (uint16_t)luaL_checkinteger(L, 4);
    st7789_draw_circle(x0, y0, r, color);
    return 0;
}

/**
 * @brief Game.fill_circle(x0, y0, r, color) - 填充圆形
 */
static int lua_game_fill_circle(lua_State *L) {
    uint16_t x0 = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t y0 = (uint16_t)luaL_checkinteger(L, 2);
    uint16_t r  = (uint16_t)luaL_checkinteger(L, 3);
    uint16_t color = (uint16_t)luaL_checkinteger(L, 4);
    st7789_fill_circle(x0, y0, r, color);
    return 0;
}

/**
 * @brief Game.draw_triangle(x1,y1, x2,y2, x3,y3, color) - 绘制三角形
 */
static int lua_game_draw_triangle(lua_State *L) {
    uint16_t x1 = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t y1 = (uint16_t)luaL_checkinteger(L, 2);
    uint16_t x2 = (uint16_t)luaL_checkinteger(L, 3);
    uint16_t y2 = (uint16_t)luaL_checkinteger(L, 4);
    uint16_t x3 = (uint16_t)luaL_checkinteger(L, 5);
    uint16_t y3 = (uint16_t)luaL_checkinteger(L, 6);
    uint16_t color = (uint16_t)luaL_checkinteger(L, 7);
    st7789_draw_triangle(x1, y1, x2, y2, x3, y3, color);
    return 0;
}

// Game 模块函数列表
static const luaL_Reg game_lib[] = {
    {"clear_screen",    lua_game_clear_screen},
    {"draw_pixel",      lua_game_draw_pixel},
    {"draw_rect",       lua_game_draw_rect},
    {"draw_text",       lua_game_draw_text},
    {"get_key",         lua_game_get_key},
    {"update_display",  lua_game_update_display},
    {"get_tick",        lua_game_get_tick},
    {"rgb",             lua_game_rgb},
    {"draw_line",       lua_game_draw_line},
    {"draw_rect_border", lua_game_draw_rect_border},
    {"draw_circle",     lua_game_draw_circle},
    {"fill_circle",     lua_game_fill_circle},
    {"draw_triangle",   lua_game_draw_triangle},
    {NULL, NULL}
};

/**
 * @brief 注册 Game 模块到 Lua 状态机
 */
void lua_binding_register_game(lua_State *L) {
    ESP_LOGI(TAG, "注册 Game 模块到 Lua...");
    luaL_newlib(L, game_lib);
    lua_setglobal(L, "Game");
}

/**
 * @brief 更新按键状态
 */
void lua_binding_update_keys(void) {
    current_key = key_get();
}