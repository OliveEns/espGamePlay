# GamePlay Console - ESP32-C3 定制游戏机

基于 ESP-IDF 框架开发的 Flash 存储式游戏机固件及配套工具。

## 📁 项目结构

```
espGamePlay/
├── main/                    # 主程序目录
│   ├── main.c               # 应用入口
│   ├── st7789.c/.h          # ST7789 显示屏驱动
│   ├── game_storage.c/.h    # Flash 游戏存储管理
│   ├── game_comm.c/.h       # 通信模块 (串口/WiFi)
│   ├── game_engine.c/.h     # 轻量级游戏引擎
│   ├── game_ui.c/.h         # 主界面 UI
│   ├── input_manager.c/.h   # 按键输入管理
│   └── CMakeLists.txt
├── tools/                   # 配套工具
│   ├── web_editor/          # 网页端游戏编辑器
│   │   ├── index.html
│   │   └── game_editor.js
│   └── pc_tool/             # PC 端传输工具
│       └── game_transfer.py
├── partitions_gameplay.csv  # Flash 分区表
├── CMakeLists.txt           # 项目根配置
├── sdkconfig               # ESP-IDF 配置
└── README.md
```

## ⚙️ 硬件配置

| 模块 | 引脚 | 说明 |
|------|------|------|
| ST7789 SPI屏 | MOSI=5, CLK=18, DC=6, CS=4, RST=-1 | 240×240 分辨率 |
| ADC按键 | IO3 | 12位ADC，五方向按键 |
| 返回键 | IO9 | 上拉输入 |
| LED指示灯 | IO19 | 高电平点亮 |
| Flash | WP=13, HD=12 | 8MB |

## 🎮 固件功能

1. **Flash分区管理**：8个游戏存储区，每个512KB
2. **通信下载**：支持串口+WiFi双模式
3. **文件系统**：游戏文件读写、索引管理
4. **主界面UI**：游戏列表、下载进度、设置菜单
5. **游戏引擎**：支持自定义游戏文件格式
6. **LED状态指示**：待机/下载中/运行中/异常

## 🛠️ 开发环境

- ESP-IDF v5.x
- VSCode + ESP-IDF 插件
- Python 3.8+ (用于PC工具)

## 🔧 编译与烧录

```bash
# 配置项目
idf.py set-target esp32c3

# 编译
idf.py build

# 烧录 (替换串口)
idf.py -p COMx flash

# 监控
idf.py -p COMx monitor
```

## 📱 外部工具使用

### 1. 网页端游戏编辑器

打开 `tools/web_editor/index.html` 在浏览器中使用：
- 🎨 画布绘图工具
- 🎯 游戏对象管理
- 📝 脚本代码编辑
- 🔧 一键打包游戏

### 2. PC端传输工具

```bash
# 安装依赖
pip install pyserial requests

# 运行
python tools/pc_tool/game_transfer.py
```

支持串口和WiFi两种传输模式，自动检测设备并发送游戏文件。

## 📦 游戏文件格式

游戏文件采用自定义二进制格式：
- 头部标识: `GAMEPLAY1.0`
- 校验和: 8位十六进制
- 数据: Base64编码的JSON

## 🎯 按键映射

| 按键 | 功能 |
|------|------|
| 上 | 向上选择/游戏中向上移动 |
| 下 | 向下选择/游戏中向下移动 |
| 左 | 删除游戏/游戏中向左移动 |
| 右 | 进入/游戏中向右移动 |
| 确认 | 确认选择/游戏中确定 |
| 返回 | 返回主页/退出游戏 |

## 🚀 整体流程

1. 网页编辑器编写游戏 → 打包导出
2. PC工具连接ESP32 → 发送游戏文件
3. ESP32存储到Flash → 显示游戏列表
4. 选择游戏 → 加载运行

## 📜 许可证

MIT License

---

**代码作者**: Oliver