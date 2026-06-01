/**
 * @file st7789.h
 * @brief ST7789 240x240 SPI LCD 显示屏驱动头文件
 * @author Oliver
 * @date 2026
 */

#ifndef ST7789_H
#define ST7789_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "st7789_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* BGR565颜色转换宏（ST7789使用BGR格式） */
#define RGB565(r, g, b) (((((b) >> 3) & 0x1F) << 11) | ((((g) >> 2) & 0x3F) << 5) | (((r) >> 3) & 0x1F))

/* 扩展颜色定义 */
#define COLOR_GRAY          RGB565(128, 128, 128)
#define COLOR_DARK_GRAY     RGB565(64, 64, 64)
#define COLOR_LIGHT_GRAY    RGB565(192, 192, 192)

/**
 * @brief 发送命令到ST7789
 * @param cmd 命令字节
 */
void st7789_send_cmd(uint8_t cmd);

/**
 * @brief 发送单字节数据到ST7789
 * @param data 数据字节
 */
void st7789_send_data(uint8_t data);

/**
 * @brief 发送多字节数据到ST7789
 * @param data 数据缓冲区指针
 * @param len 数据长度
 */
void st7789_send_data_bytes(const uint8_t *data, size_t len);

/**
 * @brief 设置显示窗口
 * @param x1 窗口左上角X坐标
 * @param y1 窗口左上角Y坐标
 * @param x2 窗口右下角X坐标
 * @param y2 窗口右下角Y坐标
 */
void st7789_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

/**
 * @brief 填充矩形区域
 * @param x1 左上角X坐标
 * @param y1 左上角Y坐标
 * @param x2 右下角X坐标
 * @param y2 右下角Y坐标
 * @param color 填充颜色 (RGB565格式)
 */
void st7789_fill_rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);

/**
 * @brief 在指定位置绘制像素点
 * @param x X坐标 (0-239)
 * @param y Y坐标 (0-239)
 * @param color 像素颜色 (RGB565格式)
 */
void st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief 清屏并填充指定颜色
 * @param color 清屏颜色 (RGB565格式)
 */
void st7789_clear(uint16_t color);

/**
 * @brief 绘制单个字符
 * @param x 字符左上角X坐标
 * @param y 字符左上角Y坐标
 * @param c 要绘制的字符 (ASCII 32-127)
 * @param color 字符颜色 (RGB565格式)
 * @param bg_color 背景颜色 (RGB565格式)
 */
void st7789_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color);

/**
 * @brief 绘制字符串
 * @param x 字符串左上角X坐标
 * @param y 字符串左上角Y坐标
 * @param str 要绘制的字符串
 * @param color 字符颜色 (RGB565格式)
 * @param bg_color 背景颜色 (RGB565格式)
 */
void st7789_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color);

/**
 * @brief 使用Bresenham算法绘制直线
 * @param x1 起点X坐标
 * @param y1 起点Y坐标
 * @param x2 终点X坐标
 * @param y2 终点Y坐标
 * @param color 直线颜色 (RGB565格式)
 */
void st7789_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);

/**
 * @brief 绘制矩形边框
 * @param x1 左上角X坐标
 * @param y1 左上角Y坐标
 * @param x2 右下角X坐标
 * @param y2 右下角Y坐标
 * @param color 边框颜色 (RGB565格式)
 */
void st7789_draw_rect_border(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);

/**
 * @brief 使用Bresenham算法绘制圆形
 * @param x0 圆心X坐标
 * @param y0 圆心Y坐标
 * @param r 圆的半径
 * @param color 圆的颜色 (RGB565格式)
 */
void st7789_draw_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);

/**
 * @brief 填充圆形区域
 * @param x0 圆心X坐标
 * @param y0 圆心Y坐标
 * @param r 圆的半径
 * @param color 填充颜色 (RGB565格式)
 */
void st7789_fill_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);

/**
 * @brief 绘制三角形
 * @param x1 顶点1 X坐标
 * @param y1 顶点1 Y坐标
 * @param x2 顶点2 X坐标
 * @param y2 顶点2 Y坐标
 * @param x3 顶点3 X坐标
 * @param y3 顶点3 Y坐标
 * @param color 三角形颜色 (RGB565格式)
 */
void st7789_draw_triangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint16_t color);

/**
 * @brief 初始化ST7789显示屏
 */
void st7789_init(void);

#ifdef __cplusplus
}
#endif

#endif /* ST7789_H */