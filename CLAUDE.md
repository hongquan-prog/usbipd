# USBIP Server - Claude 项目指南

## Linux 内核源码路径

```bash
../../src/linux-6.12.75/drivers/usb/usbip
```

## 提交代码

* 提交代码的格式参考Zephyr的提交规范
* 提交代码不要包含Claude的签名

## 代码规范

* 统一采用英语编写注释，便于国际团队合作
* 每次修改后必须使用 clang-format 格式化代码，排除 components 目录下的文件
* 一定不要使用goto语句
* 修改现有代码时不要去做一些无意义的操作，例如删除现有注释，添加空行等

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
├── src/
│   └── main.c                   # 主程序入口
├── components/
│   ├── usbipd/                  # USBIP 服务器核心组件
│   │   ├── include/
│   │   │   ├── hal/             # HAL 层头文件
│   │   │   │   ├── usbip_log.h  # 日志系统（支持颜色）
│   │   │   │   ├── usbip_osal.h # OSAL 接口
│   │   │   │   └── usbip_transport.h  # 传输层接口
│   │   │   ├── usbip_protocol.h # USBIP 协议定义
│   │   │   ├── usbip_devmgr.h   # 设备驱动接口
│   │   │   ├── usbip_server.h   # 服务器接口
│   │   │   ├── usbip_control.h  # 控制传输框架
│   │   │   ├── usbip_hid.h      # HID 设备接口
│   │   │   └── usbip_common.h   # 公共定义
│   │   └── src/
│   │       ├── hal/             # HAL 层实现
│   │       │   ├── usbip_mempool.c  # 内存池管理
│   │       │   ├── usbip_osal.c # OSAL 核心
│   │       │   └── usbip_transport.c  # 传输层核心
│   │       ├── platform/posix/  # POSIX 平台实现
│   │       │   ├── osal_posix.c # POSIX OSAL 实现
│   │       │   └── transport_tcp.c    # TCP 传输实现
│   │       ├── server/          # 服务器核心
│   │       │   ├── usbip_protocol.c   # 协议编解码
│   │       │   ├── usbip_server.c     # 服务器主循环（接受连接）
│   │       │   ├── usbip_conn.c       # 连接生命周期管理（多客户端）
│   │       │   ├── usbip_urb.c        # URB 队列处理
│   │       │   ├── usbip_devmgr.c     # 设备管理（设备绑定到连接）
│   │       │   └── usbip_control.c    # 控制传输框架
│   │       ├── device/          # 设备驱动
│   │       │   ├── usbip_hid.c  # HID 通用设备基类
│   │       │   └── usbip_bulk.c # Bulk 通用设备基类
│   │       ├── hid_dap.c        # CMSIS-DAP v1 HID 设备
│   │       └── bulk_dap.c       # CMSIS-DAP v2 Bulk 设备
│   └── debug_probe/             # CMSIS-DAP 调试探针实现
│       ├── DAP/
│       ├── debug_gpio.c
│       └── ...
└── ...
```

## 关键文件

### 核心架构
- `components/usbipd/include/hal/usbip_transport.h` - 传输层接口
- `components/usbipd/include/hal/usbip_log.h` - 日志系统接口
- `components/usbipd/include/usbip_protocol.h` - USBIP 协议定义
- `components/usbipd/include/usbip_devmgr.h` - 设备驱动接口
- `components/usbipd/include/usbip_server.h` - 服务器接口（连接状态、URB队列定义）
- `components/usbipd/src/server/usbip_server.c` - 服务器主循环（连接接受器）
- `components/usbipd/src/server/usbip_conn.c` - 连接生命周期管理（多客户端核心）
- `components/usbipd/src/server/usbip_urb.c` - URB 处理框架（每连接队列）
- `components/usbipd/src/server/usbip_devmgr.c` - 设备管理器（设备绑定到连接）
- `components/usbipd/src/platform/posix/transport_tcp.c` - TCP 传输实现

### 设备驱动
- `components/usbipd/src/hid_dap.c` - CMSIS-DAP v1 HID 设备（绑定到 `usbip_connection*`）
- `components/usbipd/src/bulk_dap.c` - CMSIS-DAP v2 Bulk 设备（绑定到 `usbip_connection*`）
- `components/usbipd/src/device/usbip_hid.c` - HID 通用设备基类
- `components/usbipd/src/device/usbip_bulk.c` - Bulk 通用设备基类

### 调试探针
- `components/debug_probe/debug_gpio.c` - GPIO bit-banging SWD 实现
- `components/debug_probe/debug_gpio.h` - GPIO 引脚定义
- `components/debug_probe/DAP/Source/DAP.c` - CMSIS-DAP 核心实现

### 编译配置
- `CMakeLists.txt` - 根目录 CMake 构建配置
- `components/usbipd/CMakeLists.txt` - USBIP 组件 CMake 配置

## 设计模式

### 传输层全局单例
- 使用 `__attribute__((constructor))` 在程序启动时自动注册
- 调用内部 `transport_register()` 注册全局实例
- 使用 wrapper 函数访问：`transport_listen()`, `transport_send()` 等

```c
/* transport_tcp.c - 自动注册 */
extern void transport_register(const char* name, struct usbip_transport* trans);

static void __attribute__((constructor)) transport_register_tcp(void)
{
    transport_register("tcp", &trans);
}
```

### 静态库构造函数保留
静态库的构造函数（`__attribute__((constructor))`）可能被链接器优化掉，使用 `-Wl,-u,symbol` 强制保留：

```cmake
# components/usbipd/CMakeLists.txt
target_link_options(${COMPONENT_NAME} INTERFACE
    -Wl,-u,osal_register_posix
    -Wl,-u,transport_register_tcp
    -Wl,-u,hid_dap_driver_register
    -Wl,-u,bulk_dap_driver_register
)
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

### 多客户端连接管理
- 每个设备连接有独立的 URB 处理线程（RX + Processor 双线程）
- 连接管理器跟踪所有活动连接，支持动态添加/移除
- 设备绑定到特定连接，防止重复导出
- 连接断开时自动解绑设备，恢复可用状态

```c
/* 连接生命周期 */
struct usbip_connection* conn = usbip_connection_create(ctx);
usbip_connection_start(conn, driver, busid);  /* 启动 RX/Processor 线程 */
/* ... URB 处理 ... */
usbip_connection_stop(conn);      /* 停止线程，解绑设备 */
usbip_connection_destroy(conn);   /* 释放资源 */

/* 设备绑定到连接 */
int usbip_bind_device(const char* busid, struct usbip_connection* conn);
void usbip_unbind_device(const char* busid);
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
CONFIG_USBIP_LOG_LEVEL=4

# 示例：URB 队列配置（高延迟网络或高吞吐量场景）
CONFIG_USBIP_URB_QUEUE_SIZE=16       # 默认 8，增大可提高吞吐量
CONFIG_USBIP_URB_DATA_MAX_SIZE=1024  # 默认 512，Bulk 设备可增大
CONFIG_USBIP_MAX_CONNECTIONS=2       # 默认 4，限制并发连接数

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

# 多客户端支持：每个设备可以被不同客户端同时连接
# 附加设备 (客户端1)
# HID DAP v1 调试器 (2-1)
sudo usbip attach -r localhost -b 2-1
# 附加设备 (客户端2)
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
- PyOCD 存在兼容性问题，需按照 `docs/PYOCD_COMPATIBILITY.md` 中的补丁进行修复
- 推荐使用 OpenOCD 进行稳定的调试操作

## 开发注意事项

1. **响应缓存**: `components/usbipd/src/hid_dap.c` 中的响应缓存逻辑需要小心处理，确保 `response_pending` 和 `response_valid` 标志正确清除
2. **IN 端点 STALL**: 当无数据时返回 `-EPIPE` 而不是 `actual_length=0`
3. **GPIO 初始化**: 使用 `pinctrl` 预先设置 GPIO 为输出模式
4. **组件化结构**: USBIP 核心实现已重构为 `components/usbipd/` 目录下的独立组件

## 测试验证结果

- OpenOCD: 完全正常 (STM32H750 烧录成功 - 5344 bytes @ 3.8 KiB/s)
- PyOCD: 完全正常