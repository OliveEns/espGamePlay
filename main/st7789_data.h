/**
 * @file st7789_data.h
 * @brief ST7789 字体数据和显示常量定义
 * @author Oliver
 * @date 2026
 */

#ifndef ST7789_DATA_H
#define ST7789_DATA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 屏幕尺寸定义 */
#define SCREEN_WIDTH     240    /*!< 屏幕宽度 */
#define SCREEN_HEIGHT    240    /*!< 屏幕高度 */

/* 字体尺寸定义 */
#define FONT_WIDTH       8      /*!< 字体宽度（像素） */
#define FONT_HEIGHT      16     /*!< 字体高度（像素） */

/* 颜色定义（BGR565格式） */
#define COLOR_BLACK      0x0000    /* BGR: 000, 000, 000 */
#define COLOR_WHITE      0xFFFF    /* BGR: 111, 111, 111 */
#define COLOR_RED        0x00F8    /* BGR: 000, 000, 111 */
#define COLOR_GREEN      0x07E0    /* BGR: 000, 111, 000 */
#define COLOR_BLUE       0xF800    /* BGR: 111, 000, 000 */
#define COLOR_YELLOW     0x07FF    /* BGR: 000, 111, 111 */
#define COLOR_MAGENTA    0xF81F    /* BGR: 111, 000, 111 */
#define COLOR_CYAN       0xFFE0    /* BGR: 111, 111, 000 */

/* ST7789 命令定义 */
#define ST7789_SWRESET   0x01  /* 软件复位 */
#define ST7789_SLPOUT    0x11  /* 退出睡眠模式 */
#define ST7789_COLMOD    0x3A  /* 设置颜色模式 */
#define ST7789_MADCTL    0x36  /* 设置内存访问控制 */
#define ST7789_DISPON    0x29  /* 开启显示 */
#define ST7789_CASET     0x2A  /* 设置列地址范围 */
#define ST7789_RASET     0x2B  /* 设置行地址范围 */
#define ST7789_RAMWR     0x2C  /* 写入显示内存 */

/**
 * @brief 8x16 ASCII 字体数据（支持 ASCII 0x20-0x7E，共96个字符）
 */
extern const uint8_t font8x16[96][16];

/**
 * @brief 获取字体数据
 * @param c 字符（ASCII 0x20-0x7E）
 * @return 指向字体数据的指针，如果字符无效则返回 NULL
 */
const uint8_t* st7789_get_font_data(char c);

#ifdef __cplusplus
}
#endif

#endif /* ST7789_DATA_H */