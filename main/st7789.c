/**
 * @file st7789.c
 * @brief ST7789 240x240 SPI LCD 显示屏驱动实现
 * @author Oliver
 * @date 2026
 */

#include "st7789.h"
#include "st7789_data.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

/* SPI硬件配置 - 保持不变 */
#define ST7789_SPI_HOST    SPI2_HOST    /* SPI主机 */
#define ST7789_SPI_MOSI    5            /* MOSI引脚 */
#define ST7789_SPI_CLK     18           /* CLK引脚 */
#define ST7789_SPI_DC      6            /* DC引脚 */
#define ST7789_SPI_CS      4            /* CS引脚 */
#define ST7789_SPI_RST     -1           /* RST引脚 (-1表示不使用硬件复位) */
#define ST7789_SPI_FREQ    40000000     /* SPI频率 40MHz */

/* 日志标签 */
static const char *TAG = "ST7789";

/* SPI设备句柄 */
static spi_device_handle_t spi_handle = NULL;

/**
 * @brief 毫秒延时函数
 * @param ms 延时毫秒数
 */
static void delay_ms(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

/**
 * @brief 设置DC引脚电平
 * @param level 电平值 (0或1)
 */
static void st7789_set_dc(uint8_t level)
{
    gpio_set_level(ST7789_SPI_DC, level);
}

/**
 * @brief 设置CS引脚电平
 * @param level 电平值 (0或1)
 */
static void st7789_set_cs(uint8_t level)
{
    gpio_set_level(ST7789_SPI_CS, level);
}

/**
 * @brief SPI发送数据
 * @param data 数据缓冲区指针
 * @param len 数据长度
 * @return ESP_OK 成功, 其他值失败
 */
static esp_err_t st7789_spi_transmit(const uint8_t *data, size_t len)
{
    spi_transaction_t t = {
        .length = len * 8,      /* 传输长度(位) */
        .tx_buffer = data,      /* 发送缓冲区 */
        .rx_buffer = NULL,      /* 接收缓冲区 */
    };
    return spi_device_transmit(spi_handle, &t);
}

/**
 * @brief 发送命令到ST7789
 * @param cmd 命令字节
 */
void st7789_send_cmd(uint8_t cmd)
{
    st7789_set_dc(0);   /* DC=0 命令模式 */
    st7789_set_cs(0);   /* CS=0 选中设备 */
    st7789_spi_transmit(&cmd, 1);
    st7789_set_cs(1);   /* CS=1 取消选中 */
}

/**
 * @brief 发送单字节数据到ST7789
 * @param data 数据字节
 */
void st7789_send_data(uint8_t data)
{
    st7789_set_dc(1);   /* DC=1 数据模式 */
    st7789_set_cs(0);   /* CS=0 选中设备 */
    st7789_spi_transmit(&data, 1);
    st7789_set_cs(1);   /* CS=1 取消选中 */
}

/**
 * @brief 发送多字节数据到ST7789
 * @param data 数据缓冲区指针
 * @param len 数据长度
 */
void st7789_send_data_bytes(const uint8_t *data, size_t len)
{
    st7789_set_dc(1);   /* DC=1 数据模式 */
    st7789_set_cs(0);   /* CS=0 选中设备 */
    st7789_spi_transmit(data, len);
    st7789_set_cs(1);   /* CS=1 取消选中 */
}

/**
 * @brief 设置显示窗口
 * @param x1 窗口左上角X坐标
 * @param y1 窗口左上角Y坐标
 * @param x2 窗口右下角X坐标
 * @param y2 窗口右下角Y坐标
 */
void st7789_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    /* 设置列地址范围 */
    st7789_send_cmd(ST7789_CASET);
    uint8_t data[4];
    data[0] = (x1 >> 8) & 0xFF;   /* X1高字节 */
    data[1] = x1 & 0xFF;           /* X1低字节 */
    data[2] = (x2 >> 8) & 0xFF;   /* X2高字节 */
    data[3] = x2 & 0xFF;           /* X2低字节 */
    st7789_send_data_bytes(data, 4);

    /* 设置行地址范围 */
    st7789_send_cmd(ST7789_RASET);
    data[0] = (y1 >> 8) & 0xFF;   /* Y1高字节 */
    data[1] = y1 & 0xFF;           /* Y1低字节 */
    data[2] = (y2 >> 8) & 0xFF;   /* Y2高字节 */
    data[3] = y2 & 0xFF;           /* Y2低字节 */
    st7789_send_data_bytes(data, 4);

    /* 进入内存写入模式 */
    st7789_send_cmd(ST7789_RAMWR);
}

/**
 * @brief 填充矩形区域
 * @param x1 左上角X坐标
 * @param y1 左上角Y坐标
 * @param x2 右下角X坐标
 * @param y2 右下角Y坐标
 * @param color 填充颜色 (RGB565格式)
 */
void st7789_fill_rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color)
{
    // 确保坐标顺序正确
    int16_t xs = x1, xe = x2, ys = y1, ye = y2;
    if (xs > xe) { int16_t t = xs; xs = xe; xe = t; }
    if (ys > ye) { int16_t t = ys; ys = ye; ye = t; }

    // 裁剪到屏幕范围内
    if (xs >= SCREEN_WIDTH) return;
    if (ys >= SCREEN_HEIGHT) return;
    if (xe < 0) return;
    if (ye < 0) return;
    if (xs < 0) xs = 0;
    if (ys < 0) ys = 0;
    if (xe >= SCREEN_WIDTH) xe = SCREEN_WIDTH - 1;
    if (ye >= SCREEN_HEIGHT) ye = SCREEN_HEIGHT - 1;

    // 重新计算宽高
    uint32_t width = xe - xs + 1;
    uint32_t height = ye - ys + 1;
    uint32_t total = width * height;

    /* 设置窗口 */
    st7789_set_window(xs, ys, xe, ye);

    /* 使用缓冲区减少SPI传输次数，避免看门狗超时 */
    #define FILL_BUFFER_SIZE 512
    uint8_t buffer[FILL_BUFFER_SIZE];

    uint8_t color_high = (color >> 8) & 0xFF;
    uint8_t color_low = color & 0xFF;

    for (uint32_t i = 0; i < FILL_BUFFER_SIZE; i += 2) {
        buffer[i] = color_high;
        buffer[i+1] = color_low;
    }

    st7789_set_dc(1);
    st7789_set_cs(0);

    uint32_t remaining = total;
    while (remaining > 0) {
        uint32_t pixels_to_send = remaining;
        if (pixels_to_send > FILL_BUFFER_SIZE / 2) {
            pixels_to_send = FILL_BUFFER_SIZE / 2;
        }
        st7789_spi_transmit(buffer, pixels_to_send * 2);
        remaining -= pixels_to_send;
        taskYIELD();
    }

    st7789_set_cs(1);
}

/**
 * @brief 在指定位置绘制像素点
 * @param x X坐标 (0-239)
 * @param y Y坐标 (0-239)
 * @param color 像素颜色 (RGB565格式)
 */
void st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    st7789_set_window(x, y, x, y);
    uint8_t color_bytes[2] = {
        (color >> 8) & 0xFF,
        color & 0xFF
    };
    st7789_send_data_bytes(color_bytes, 2);
}

/**
 * @brief 清屏并填充指定颜色
 * @param color 清屏颜色 (RGB565格式)
 */
void st7789_clear(uint16_t color)
{
    st7789_fill_rect(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, color);
}

/**
 * @brief 绘制单个字符
 * @param x 字符左上角X坐标
 * @param y 字符左上角Y坐标
 * @param c 要绘制的字符 (ASCII 32-127)
 * @param color 字符颜色 (RGB565格式)
 * @param bg_color 背景颜色 (RGB565格式)
 */
void st7789_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color)
{
    /* 检查字符范围 */
    if (c < 32 || c > 127) {
        return;
    }

    /* 获取字体数据 */
    const uint8_t *font_data = st7789_get_font_data(c);
    if (font_data == NULL) {
        return;
    }
    
    /* 设置字符显示窗口 (8x16) */
    st7789_set_window(x, y, x + FONT_WIDTH - 1, y + FONT_HEIGHT - 1);
    
    /* 准备字符数据缓冲区 */
    uint8_t buffer[FONT_WIDTH * FONT_HEIGHT * 2];  /* 8x16 = 128像素 = 256字节 */
    uint8_t color_high = (color >> 8) & 0xFF;
    uint8_t color_low = color & 0xFF;
    uint8_t bg_high = (bg_color >> 8) & 0xFF;
    uint8_t bg_low = bg_color & 0xFF;
    
    uint16_t buf_idx = 0;
    for (uint8_t row = 0; row < FONT_HEIGHT; row++) {
        uint8_t byte = font_data[row];
        for (uint8_t col = 0; col < FONT_WIDTH; col++) {
            if (byte & (1 << (7 - col))) {
                /* 显示字符像素 */
                buffer[buf_idx++] = color_high;
                buffer[buf_idx++] = color_low;
            } else {
                /* 显示背景像素 */
                buffer[buf_idx++] = bg_high;
                buffer[buf_idx++] = bg_low;
            }
        }
    }
    
    /* 一次性发送字符数据 */
    st7789_set_dc(1);
    st7789_set_cs(0);
    st7789_spi_transmit(buffer, sizeof(buffer));
    st7789_set_cs(1);
}

/**
 * @brief 绘制字符串
 * @param x 字符串左上角X坐标
 * @param y 字符串左上角Y坐标
 * @param str 要绘制的字符串
 * @param color 字符颜色 (RGB565格式)
 * @param bg_color 背景颜色 (RGB565格式)
 */
void st7789_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color)
{
    uint16_t x_pos = x;
    while (*str) {
        st7789_draw_char(x_pos, y, *str, color, bg_color);
        x_pos += FONT_WIDTH;  /* 每个字符宽度8像素 */
        str++;
    }
}

/**
 * @brief 使用Bresenham算法绘制直线
 * @param x1 起点X坐标
 * @param y1 起点Y坐标
 * @param x2 终点X坐标
 * @param y2 终点Y坐标
 * @param color 直线颜色 (RGB565格式)
 */
void st7789_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    int16_t dx = abs((int16_t)(x2 - x1));
    int16_t dy = abs((int16_t)(y2 - y1));
    int16_t sx = (x1 < x2) ? 1 : -1;
    int16_t sy = (y1 < y2) ? 1 : -1;
    int16_t err = dx - dy;

    while (1) {
        st7789_draw_pixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) {
            break;
        }
        int16_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

/**
 * @brief 绘制矩形边框
 * @param x1 左上角X坐标
 * @param y1 左上角Y坐标
 * @param x2 右下角X坐标
 * @param y2 右下角Y坐标
 * @param color 边框颜色 (RGB565格式)
 */
void st7789_draw_rect_border(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    st7789_draw_line(x1, y1, x2, y1, color);  /* 上边 */
    st7789_draw_line(x1, y2, x2, y2, color);  /* 下边 */
    st7789_draw_line(x1, y1, x1, y2, color);  /* 左边 */
    st7789_draw_line(x2, y1, x2, y2, color);  /* 右边 */
}

/**
 * @brief 使用Bresenham算法绘制圆形
 * @param x0 圆心X坐标
 * @param y0 圆心Y坐标
 * @param r 圆的半径
 * @param color 圆的颜色 (RGB565格式)
 */
void st7789_draw_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
    int16_t x = -r;
    int16_t y = 0;
    int16_t err = 2 - 2 * r;

    do {
        /* 绘制对称的四个点 */
        st7789_draw_pixel(x0 - x, y0 + y, color);
        st7789_draw_pixel(x0 + x, y0 + y, color);
        st7789_draw_pixel(x0 + x, y0 - y, color);
        st7789_draw_pixel(x0 - x, y0 - y, color);
        
        int16_t e2 = err;
        if (e2 <= y) {
            err += ++y * 2 + 1;
            if (-x == y && e2 <= x) {
                e2 = 0;
            }
        }
        if (e2 > x) {
            err += ++x * 2 + 1;
        }
    } while (x < 0);
}

/**
 * @brief 填充圆形区域
 * @param x0 圆心X坐标
 * @param y0 圆心Y坐标
 * @param r 圆的半径
 * @param color 填充颜色 (RGB565格式)
 */
void st7789_fill_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
    int16_t x = -r;
    int16_t y = 0;
    int16_t err = 2 - 2 * r;

    do {
        /* 确保矩形坐标正确 */
        uint16_t x1 = x0 + x;
        uint16_t x2 = x0 - x;
        if (x1 > x2) {
            uint16_t tmp = x1;
            x1 = x2;
            x2 = tmp;
        }
        
        /* 绘制上下两条水平线 */
        uint16_t y1_top = y0 - y;
        uint16_t y1_bottom = y0 + y;
        
        st7789_fill_rect(x1, y1_top, x2, y1_top, color);
        if (y != 0) {  /* 避免重复绘制中心线 */
            st7789_fill_rect(x1, y1_bottom, x2, y1_bottom, color);
        }
        
        int16_t e2 = err;
        if (e2 <= y) {
            err += ++y * 2 + 1;
            if (-x == y && e2 <= x) {
                e2 = 0;
            }
        }
        if (e2 > x) {
            err += ++x * 2 + 1;
        }
    } while (x < 0);
}

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
void st7789_draw_triangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint16_t color)
{
    st7789_draw_line(x1, y1, x2, y2, color);
    st7789_draw_line(x2, y2, x3, y3, color);
    st7789_draw_line(x3, y3, x1, y1, color);
}

/**
 * @brief 初始化ST7789显示屏
 */
void st7789_init(void)
{
    ESP_LOGI(TAG, "正在初始化ST7789显示屏...");

    /* 配置GPIO引脚 */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ST7789_SPI_DC) | (1ULL << ST7789_SPI_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    /* 初始化SPI总线 */
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,           /* 不使用MISO */
        .mosi_io_num = ST7789_SPI_MOSI,
        .sclk_io_num = ST7789_SPI_CLK,
        .quadwp_io_num = -1,         /* 不使用四线模式 */
        .quadhd_io_num = -1,         /* 不使用四线模式 */
        .max_transfer_sz = 4096      /* 最大传输大小 */
    };

    /* 配置SPI设备 */
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = ST7789_SPI_FREQ,
        .mode = 0,                   /* SPI模式0 */
        .spics_io_num = ST7789_SPI_CS,
        .queue_size = 7,             /* 队列大小 */
        .pre_cb = NULL               /* 传输前回调 */
    };

    /* 初始化SPI总线 */
    esp_err_t ret = spi_bus_initialize(ST7789_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    /* 添加SPI设备 */
    ret = spi_bus_add_device(ST7789_SPI_HOST, &devcfg, &spi_handle);
    ESP_ERROR_CHECK(ret);

    /* ST7789初始化序列 */
    st7789_set_cs(1);  /* 初始取消片选 */
    delay_ms(10);

    /* 软件复位 */
    st7789_send_cmd(ST7789_SWRESET);
    delay_ms(150);

    /* 退出睡眠模式 */
    st7789_send_cmd(ST7789_SLPOUT);
    delay_ms(10);

    /* 设置颜色模式为16位RGB565 */
    st7789_send_cmd(ST7789_COLMOD);
    st7789_send_data(0x55);  /* 16-bit color */
    delay_ms(10);

    /* 设置内存访问控制 */
    st7789_send_cmd(ST7789_MADCTL);
    st7789_send_data(0x40);  /* MY bit set - 垂直镜像 */

    /* 开启显示 */
    st7789_send_cmd(ST7789_DISPON);
    delay_ms(100);

    /* 清屏 */
    st7789_clear(COLOR_BLACK);

    ESP_LOGI(TAG, "ST7789显示屏初始化完成");
}