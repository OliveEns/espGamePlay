# espGamePlay — ESP32-C3 游戏控制台

基于 ESP32-C3 + ST7789 240×240 LCD 的 Lua 可编程游戏机，支持通过串口/WiFi 下载 `.game` 游戏文件，内置 Lua 5.3 脚本引擎。

---

## 目录

- [硬件配置](#硬件配置)
- [项目结构](#项目结构)
- [快速开始](#快速开始)
- [Lua 游戏编写规则](#lua-游戏编写规则)
- [Game API 参考](#game-api-参考)
- [打包与部署](#打包与部署)
- [示例游戏](#示例游戏)

---

## 硬件配置

| 项目 | 规格 |
|------|------|
| **主控芯片** | ESP32-C3 (RISC-V 160MHz) |
| **显示屏** | ST7789 240×240 SPI LCD |
| **色彩格式** | RGB565 (BGR 字节序) |
| **SPI 频率** | 40 MHz |

### 引脚定义

| 功能 | GPIO |
|------|------|
| SPI MOSI | 5 |
| SPI CLK | 18 |
| DC (Data/Command) | 6 |
| CS (Chip Select) | 4 |
| RST | -1 (不使用硬件复位) |

### 按键定义

| 按键 | 类型 | GPIO / ADC 阈值 |
|------|------|-----------------|
| **UP** | ADC (IO3) | ADC 值 2400~3100 |
| **DOWN** | ADC (IO3) | ADC 值 0~800 |
| **LEFT** | ADC (IO3) | ADC 值 800~1600 |
| **RIGHT** | ADC (IO3) | ADC 值 1600~2400 |
| **OK** | ADC (IO3) | ADC 值 3100~3900 |
| **BACK** | GPIO9 数字输入 | 低电平有效 |

> ADC 按键通过电阻分压网络实现，不同按键产生不同电压。

### 存储

- 游戏文件存储在 SPIFFS 分区 (`game_storage`) 中
- 挂载路径：`/game`
- 文件扩展名：`.game`

---

## 项目结构

```
espGamePlay/
├── main/
│   ├── main.c              # 主程序入口、菜单系统
│   ├── st7789.c/h          # ST7789 显示屏驱动
│   ├── st7789_data.c/h     # 字库数据
│   ├── key_input.c/h       # 按键输入模块
│   ├── lua_binding.c/h     # Lua 硬件绑定层 (Game API)
│   ├── game_manager.c/h    # 游戏文件管理 (.game 格式)
│   ├── game_task.c/h       # 游戏任务调度 (FreeRTOS)
│   ├── download_manager.c/h # 串口/WiFi 下载
│   └── CMakeLists.txt
├── tools/
│   ├── pack_game.py         # Lua→.game 打包工具
│   ├── upload.py            # 串口上传工具
│   └── test/
│       └── snake.lua        # 贪吃蛇游戏
└── README.md
```

---

## 快速开始

### 1. 编译固件

```bash
idf.py build
```

### 2. 烧录到 ESP32-C3

```bash
idf.py -p COMx flash monitor
```

### 3. 编写游戏脚本

参考下方的 [Lua 游戏编写规则](#lua-游戏编写规则) 编写 `.lua` 脚本。

### 4. 打包 .game 文件

```bash
python tools/pack_game.py your_game.lua 游戏名称 作者名
```

示例：
```bash
python tools/pack_game.py tools/test/snake.lua snake Oliver
```

### 5. 部署到设备

使用 `upload.py` 上传 `.game` 文件到游戏机。

**串口上传：**

由于和烧录串口一致，因此上传时需关闭串口监控。

先在游戏机菜单选择 "Serial Download"，然后运行：

```bash
python tools/upload.py serial <COM端口> <游戏文件.game>
```

示例：
```bash
python tools/upload.py serial COM3 snake.game
```

**WiFi 上传：**

先在游戏机菜单选择 "WiFi Download"，设备会创建热点 `ESP_Game_AP`（密码 `12345678`），电脑连接该热点后运行：

```bash
python tools/upload.py tcp <设备IP> <游戏文件.game>
```

示例：
```bash
python tools/upload.py tcp 192.168.4.1 snake.game
```

---

## Lua 游戏编写规则

### 游戏生命周期

每个游戏脚本 **必须** 实现以下三个全局函数，系统会按顺序调用：

| 函数 | 调用时机 | 说明 |
|------|---------|------|
| `function on_init()` | 游戏启动时调用 **一次** | 用于初始化变量、清屏、绘制初始画面 |
| `function on_update(dt)` | 每帧调用 | `dt` 为帧间隔时间（秒，float），用于处理逻辑、按键、移动 |
| `function on_render()` | 每帧调用 | 用于绘制画面。**注意：无硬件双缓冲，频繁全屏重绘会导致闪烁** |

### 游戏循环

```
┌──────────────────────────────────────┐
│  on_init()  — 初始化（仅一次）        │
│  ↓                                   │
│  ┌────────────────────────┐          │
│  │ on_update(dt)  (逻辑)  │  ← 每帧   │
│  │ on_render()    (渲染)  │  ← 每帧   │
│  └────────────────────────┘          │
│  ↓ 按 BACK 键退出                     │
│  返回主菜单                           │
└──────────────────────────────────────┘
```

### 编写建议

1. **避免在 `on_render()` 中调用 `clear_screen` 后再全屏重绘**，会导致 SPI 屏幕闪烁。推荐增量绘制：在 `on_init()` 中绘制静态背景，`on_render()` 中只更新变化的部分。
2. 屏幕坐标范围：**X: 0~239, Y: 0~239**。
3. 颜色使用 `Game.rgb(r, g, b)` 转换，参数范围 0~255。
4. 可以使用 Lua 标准库（`math`、`string`、`table` 等）。
5. `Game.draw_text` 使用黑色背景，在非黑色背景上使用时请注意。

---

## Game API 参考

所有绘图和输入函数通过全局 `Game` 表调用。

### 显示函数

#### `Game.clear_screen(color)`
清屏并填充指定颜色。
- `color` — RGB565 颜色值

#### `Game.draw_pixel(x, y, color)`
绘制单个像素。
- `x, y` — 坐标 (0~239)
- `color` — RGB565 颜色值

#### `Game.draw_rect(x, y, w, h, color)`
绘制填充矩形。
- `x, y` — 左上角坐标
- `w, h` — 宽、高
- 实际填充范围：`(x, y)` 到 `(x+w-1, y+h-1)`

#### `Game.draw_rect_border(x, y, w, h, color)`
绘制矩形边框（空心）。
- 参数同 `draw_rect`

#### `Game.draw_line(x1, y1, x2, y2, color)`
使用 Bresenham 算法绘制直线。
- `x1, y1` — 起点
- `x2, y2` — 终点

#### `Game.draw_circle(x0, y0, r, color)`
绘制圆形边框（Bresenham 算法）。
- `x0, y0` — 圆心
- `r` — 半径

#### `Game.fill_circle(x0, y0, r, color)`
绘制填充圆形。
- 参数同 `draw_circle`

#### `Game.draw_triangle(x1, y1, x2, y2, x3, y3, color)`
绘制三角形边框。
- `x1,y1`, `x2,y2`, `x3,y3` — 三个顶点

#### `Game.draw_text(x, y, text, color)`
绘制字符串（8×12 像素 ASCII 字体）。
- `x, y` — 左上角坐标
- `text` — 字符串（仅支持 ASCII 32-127）
- 背景色固定为黑色

#### `Game.update_display()`
刷新显示。当前实现为空操作（ST7789 直接写屏）。

### 输入函数

#### `Game.get_key()`
返回当前按下的按键名称（字符串）。

| 返回值 | 含义 |
|--------|------|
| `"NONE"` | 无按键 |
| `"UP"` | 上 |
| `"DOWN"` | 下 |
| `"LEFT"` | 左 |
| `"RIGHT"` | 右 |
| `"OK"` | 确认 |
| `"BACK"` | 返回 |

> 注意：`BACK` 键由系统框架捕获用于退出游戏，脚本中不会收到 `"BACK"`。

### 工具函数

#### `Game.rgb(r, g, b)`
将 8-bit RGB 分量转换为 RGB565 颜色值。
- `r, g, b` — 红、绿、蓝分量（0~255）
- 返回值：RGB565 格式的 16-bit 颜色值

#### `Game.get_tick()`
获取系统启动以来的毫秒时间戳。
- 返回值：毫秒数（整数）

---

## 打包与部署

### .game 文件格式

游戏文件由 **64 字节文件头** + **Lua 脚本数据** 组成：

```
偏移  大小  字段
0     4     魔数 "GM01"
4     1     版本号 (0x01)
5     1     保留
6     2     游戏名称长度
8     32    游戏名称 (UTF-8, 不足补 '\0')
40    16    作者名称 (UTF-8, 不足补 '\0')
56    4     脚本 CRC32 校验
60    4     脚本字节数
64    N     Lua 脚本源码
```

### 打包命令

```bash
python tools/pack_game.py <input.lua> <游戏名称> <作者> [output.game]
```

示例：
```bash
python tools/pack_game.py snake.lua 贪吃蛇 Oliver snake.game
```

---

## 示例游戏

### 贪吃蛇 (`tools/test/snake.lua`)

完整贪吃蛇实现，演示了：
- 增量渲染（避免全屏闪烁）
- 网格对齐坐标计算
- 按键防抖处理
- 碰撞检测
- 游戏状态管理