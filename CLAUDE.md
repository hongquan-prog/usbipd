# USBIP Server - Claude 项目指南

## Linux 内核源码路径

```bash
../../src/linux-6.12.75/drivers/usb/usbip
```

## 代码规范

* 统一采用英语编写注释，便于国际团队合作
* 每次修改后必须使用 clang-format 格式化代码，排除 components 目录下的文件

### 配置文件
- `.clang-format` - ClangFormat 配置
- `Kconfig` - Kconfig 配置选项定义
- `.config` - 用户配置（由 gen_config.py 生成）
- `build/config.cmake` - 编译定义（自动生成）

### 架构设计
- 每次修改后同步根目录下的 `docs/ARCHITECTURE.md` 文件

### C 代码风格

**缩进与空格**
- 4 空格缩进，不使用 Tab
- 行宽 100 字符
- 文件末尾保留空行
- 行尾不留空格

**命名约定**
```c
// 函数名: 小写+下划线
void usbip_send_header(void);

// 结构体名: 小写+下划线
struct usbip_header;

// 宏定义: 大写+下划线
#define USBIP_CMD_SUBMIT 0x0001

// 私有静态函数: 前缀 static
static void handle_urb(void);
```

**大括号风格**
```c
// 函数大括号换行
int function(void)
{
    return 0;
}

// 控制语句大括号换行
if (condition)
{
    do_something();
}
else
{
    do_other();
}
```

**空行约定**
```c
// if/else/switch/while/for 等控制语句的花括号后空一行
if (condition)
{
    do_something();
}

next_statement();  // 空一行

while (running)
{
    loop();
}

cleanup();  // 空一行

switch (val)
{
case 1:
    handle_one();
    break;
default:
    break;
}

finalize();  // 空一行

// do-while 同样遵循
do
{
    work();
} while (more);

finish();
```

*注意：clang-format 不会自动添加这些空行，需要手动维护*

**注释风格**

```c
/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 *
 */

/**
 * 函数文档 - Doxygen 风格
 * @param param 参数说明
 * Return: 返回值说明
 */

/* Listen for connections */
err = listen(listen_sock, 1);
if (err != 0)
{
    os_printf("Error occured during listen: errno %d\r\n", errno);
    break;
}

```

**包含顺序**
1. 系统头文件 `<*.h>`
2. 项目头文件 `"*.h"`

**变量定义**

临时变量定义在函数最前面，按照先声明后使用的顺序排列：

```c
static int example_function(struct usbip_transport* trans, uint16_t port)
{
    struct tcp_transport_priv* priv = trans->priv;  /* 先声明所有变量 */
    int opt = 1;
    struct sockaddr_in addr;

    priv->fd = socket(AF_INET, SOCK_STREAM, 0);     /* 后写代码逻辑 */
    if (priv->fd < 0)
    {
        return -1;
    }

    ...
}
```

### 格式化命令

```bash
# 格式化单个文件
clang-format -i src/main.c

# 格式化所有 C 文件（排除 components）
find . -name "*.c" -o -name "*.h" | grep -v components | xargs clang-format -i
```

### 编译命令

```bash
# 首次配置（自动创建 build 目录）
cmake -B build -S .

# 编译
cmake --build build
```

## 项目概述

这是一个 USBIP 服务器实现，运行在 Raspberry Pi 5 上，采用三层架构设计，支持多种虚拟 USB 设备：
- **MSC 大容量存储** (1-2)
- **CDC ACM 串口** (1-3)
- **CMSIS-DAP v2 Bulk 调试器** (2-2) - 主要使用
- **CMSIS-DAP v1 HID 调试器** (2-1)

## 架构文档

- `docs/ARCHITECTURE.md` - 详细架构设计文档

## 项目结构

```
usbip-server/
├── include/
│   ├── hal/                    # HAL 层头文件
│   │   ├── usbip_log.h         # 日志系统（支持颜色）
│   │   ├── usbip_osal.h        # OSAL 接口
│   │   └── usbip_transport.h   # 传输层接口
│   ├── usbip_protocol.h         # USBIP 协议定义
│   ├── usbip_devmgr.h           # 设备驱动接口
│   ├── usbip_server.h           # 服务器接口
│   └── ...
├── src/
│   ├── hal/                     # HAL 层实现
│   │   ├── usbip_log.c          # 日志系统
│   │   ├── usbip_osal.c         # OSAL 核心
│   │   └── usbip_transport.c    # 传输层核心
│   ├── transport_tcp.c          # TCP 传输实现（自动注册）
│   ├── server/                  # 服务器核心
│   │   ├── usbip_protocol.c     # 协议编解码
│   │   ├── usbip_server.c       # 连接管理
│   │   ├── usbip_urb.c          # URB 处理
│   │   ├── usbip_devmgr.c       # 设备管理
│   │   └── usbip_control.c      # 控制传输框架
│   ├── device/                  # 设备驱动
│   │   ├── usbip_hid.c          # HID 基类
│   │   ├── usbip_bulk.c         # Bulk 基类
│   │   └── ...
│   └── main.c                   # 主程序
└── components/                  # 第三方组件
    └── debug_probe/             # CMSIS-DAP 实现
```

## 关键文件

### 核心架构
- `include/hal/usbip_transport.h` - 传输层接口
- `include/hal/usbip_log.h` - 日志系统接口
- `include/usbip_protocol.h` - USBIP 协议定义
- `include/usbip_devmgr.h` - 设备驱动接口
- `src/server/usbip_urb.c` - URB 处理框架
- `src/server/usbip_devmgr.c` - 设备管理器
- `src/transport_tcp.c` - TCP 传输实现

### 设备驱动
- `src/hid_dap.c` - CMSIS-DAP v1 HID 设备
- `src/bulk_dap.c` - CMSIS-DAP v2 Bulk 设备 (主要调试设备)
- `src/device/usbip_hid.c` - HID 通用设备基类
- `src/device/usbip_bulk.c` - Bulk 通用设备基类

### 调试探针
- `components/debug_probe/debug_gpio.c` - GPIO bit-banging SWD 实现
- `components/debug_probe/debug_gpio.h` - GPIO 引脚定义
- `components/debug_probe/swd.c` - SWD 协议实现
- `components/debug_probe/DAP/Source/DAP.c` - CMSIS-DAP 核心实现

### 编译配置
- `CMakeLists.txt` - CMake 构建配置

## 设计模式

### 传输层全局单例
- 使用 `__attribute__((constructor))` 在程序启动时自动注册
- 通过 `transport_set_global()` 设置全局实例
- 使用 wrapper 函数访问：`transport_listen()`, `transport_send()` 等

```c
/* transport_tcp.c - 自动注册 */
static void __attribute__((constructor)) tcp_transport_register(void)
{
    struct usbip_transport* trans = tcp_transport_create();
    if (trans)
    {
        transport_set_global(trans);
    }
}
```

### 日志系统
- 支持彩色输出（通过 `LOG_USE_COLOR` 控制）
- 使用 `LOG_MODULE_REGISTER(name, level)` 注册模块
- 日志级别由 Kconfig 配置决定（如 `CONFIG_MAIN_LOG_LEVEL`）

```c
LOG_MODULE_REGISTER(main, CONFIG_MAIN_LOG_LEVEL);

LOG_ERR("Error: %d", err);
LOG_WRN("Warning: %s", msg);
LOG_INF("Info: %s", msg);
LOG_DBG("Debug: %s", msg);
```

## GPIO 引脚配置 (Raspberry Pi 5)

| 功能 | GPIO | 物理引脚 |
|------|------|----------|
| SWCLK | 17 | Pin 11 |
| SWDIO | 27 | Pin 13 |

## 常用命令

### 配置
```
cmake -B build -S .
```

### 编译
```bash
cmake --build build
```

### Kconfig 配置

项目使用类似 Linux 内核的 Kconfig 配置系统。

```bash
# 1. 编辑 .config 文件（参考 Kconfig）
vim .config

# 示例：修改端口和日志级别
CONFIG_USBIP_SERVER_PORT=3241
CONFIG_LOG_SERVER_LEVEL=4

# 2. 重新生成配置
python scripts/gen_config.py

# 3. 重新编译
cmake -B build -S .
cmake --build build
```

#### 日志等级

| 值 | 级别 |
|----|------|
| 0 | LOG_LEVEL_NONE |
| 1 | LOG_LEVEL_ERR |
| 2 | LOG_LEVEL_WRN |
| 3 | LOG_LEVEL_INF (默认) |
| 4 | LOG_LEVEL_DBG |

#### 重置到默认值
```bash
rm .config
python scripts/gen_config.py
```

### 运行服务器
```bash
# 加载内核相关模块
sudo modprobe usbip-core
sudo modprobe usbip-host
sudo modprobe vhci-hcd

# 启动服务器后台运行
sudo build/usbip-server &

# 服务器一次只能连接一个设备，只能同时连接一个设备
# 附加设备
# HID DAP v1 调试器 (2-1)
sudo usbip attach -r localhost -b 2-1
# Bulk DAP v2 调试器 (2-2)
sudo usbip attach -r localhost -b 2-2
```

### OpenOCD 烧录

```bash
# OpenOCD 烧录测试 (2-1)
./test_flash.sh test/LED.hex openocd hid
# OpenOCD 烧录测试 (2-2)
./test_flash.sh test/LED.hex openocd bulk
# PyOCD 烧录测试 (2-1)
./test_flash.sh test/LED.hex pyocd hid
# PyOCD 烧录测试 (2-2)
./test_flash.sh test/LED.hex pyocd bulk
```

## 已知问题

### PyOCD 兼容性
- PyOCD 与虚拟 Bulk 设备 (2-2) 存在兼容性问题
- 推荐使用 OpenOCD 进行稳定的调试操作

## 开发注意事项

1. **响应缓存**: `hid_dap.c` 中的响应缓存逻辑需要小心处理，确保 `response_pending` 和 `response_valid` 标志正确清除
2. **IN 端点 STALL**: 当无数据时返回 `-EPIPE` 而不是 `actual_length=0`
3. **GPIO 初始化**: 使用 `pinctrl` 预先设置 GPIO 为输出模式

## 测试验证结果

- OpenOCD: 完全正常 (STM32H750 烧录成功 - 5344 bytes @ 3.8 KiB/s)
- PyOCD: 完全正常