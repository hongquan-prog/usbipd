# USBIP Server

[English](README.md) | [中文](README_zh.md)

USBIP 服务器实现，运行在 Raspberry Pi 5 上，支持 CMSIS-DAP 虚拟调试设备。

## 架构设计

```
┌─────────────────────────────────────────────────────────┐
│                      main.c                              │
│                   (服务器主循环)                          │
└──────────────────────┬──────────────────────────────────┘
                       │
         ┌─────────────┼─────────────┐
         │             │             │
         ▼             ▼             ▼
┌─────────────┐ ┌─────────────┐ ┌─────────────┐
│ transport.h │ │usbip_proto.h│ │device_drv.h │
│  (传输层)    │ │  (协议层)    │ │ (设备驱动)  │
└─────────────┘ └─────────────┘ └─────────────┘
         │             │             │
         ▼             ▼             ▼
┌─────────────┐ ┌─────────────┐ ┌─────────────┐
│transport_tcp│ │usbip_proto.c│ │ virtual_*   │
│    .c       │ │             │ │    .c       │
└─────────────┘ └─────────────┘ └─────────────┘
```

1. **传输层** - 抽象网络传输接口，支持 TCP/串口/自定义
2. **协议层** - USBIP 协议编解码，与传输层解耦
3. **设备驱动层** - 设备驱动抽象接口，实现自定义 USB 设备

---

## 虚拟设备

### 1. CMSIS-DAP v1 HID 调试器 (Bus ID: 2-1)
- **VID:PID**: c251:4001
- **接口**: HID (03:00:00)
- **端点**: EP1 IN/OUT (Interrupt)
- **功能**: SWD 调试协议，支持 OpenOCD/PyOCD

### 2. CMSIS-DAP v2 Bulk 调试器 (Bus ID: 2-2) - 主要使用
- **VID:PID**: c251:4002
- **接口**: Vendor Specific (FF:00:00)
- **端点**: EP1 IN/OUT (Bulk, 64 bytes)
- **功能**: SWD 调试协议，高速传输

---

## 目录结构

```
usbip-server/
├── components/
│   ├── usbipd/                    # USBIP 服务器核心组件
│   │   ├── include/               # 公共头文件
│   │   │   ├── hal/               # HAL 层头文件
│   │   │   │   ├── usbip_log.h    # 日志系统
│   │   │   │   ├── usbip_osal.h   # OSAL 接口
│   │   │   │   └── usbip_transport.h  # 传输层接口
│   │   │   ├── usbip_common.h     # 公共定义与协议常量
│   │   │   ├── usbip_devmgr.h     # 设备驱动层接口
│   │   │   ├── usbip_server.h     # 服务器主接口
│   │   │   ├── usbip_control.h    # 控制传输框架
│   │   │   └── usbip_hid.h        # HID 设备接口
│   │   ├── priv/                  # 私有头文件（内部使用）
│   │   │   ├── usbip_conn.h       # 连接管理（内部）
│   │   │   ├── usbip_pack.h       # 字节序转换函数
│   │   │   └── usbip_urb.h        # URB 队列接口（内部）
│   │   └── src/                   # 实现代码
│   │       ├── hal/               # HAL 层实现
│   │       │   ├── usbip_log.c    # 日志系统
│   │       │   ├── usbip_osal.c   # OSAL 核心
│   │       │   ├── usbip_transport.c  # 传输层核心
│   │       │   └── usbip_mempool.c    # 内存池
│   │       ├── server/            # 服务器核心
│   │       │   ├── usbip_pack.c   # 字节序转换/打包函数
│   │       │   ├── usbip_server.c # 连接管理
│   │       │   ├── usbip_urb.c    # URB 处理
│   │       │   ├── usbip_devmgr.c # 设备管理
│   │       │   └── usbip_control.c    # 控制传输框架
│   │       ├── device/            # 设备驱动基类
│   │       │   ├── usbip_hid.c    # HID 基类
│   │       │   └── usbip_bulk.c   # Bulk 基类
│   │       ├── platform/posix/    # POSIX 平台实现
│   │       │   ├── osal_posix.c   # POSIX OSAL 实现
│   │       │   └── transport_tcp.c    # TCP 传输实现
│   │       ├── hid_dap.c          # HID DAP v1 驱动
│   │       └── bulk_dap.c         # Bulk DAP v2 驱动
│   └── debug_probe/               # CMSIS-DAP 实现
│       ├── debug_gpio.c           # GPIO bit-banging
│       ├── debug_gpio.h           # GPIO 定义
│       ├── swd.c                  # SWD 协议
│       └── DAP/                   # CMSIS-DAP 核心
├── src/
│   └── main.c                     # 主程序入口
├── Kconfig                        # Kconfig 配置
├── scripts/
│   └── gen_config.py              # 配置生成脚本
└── README.md
```

---

## 编译

```bash
# 配置和编译
cmake -B build -S .
cmake --build build

# 调试模式
cmake -B build -S . -DDEBUG=ON
cmake --build build
```

---

## 配置

项目使用类似 Linux 内核的 Kconfig 配置系统。

### 配置文件

- `Kconfig` - 配置选项定义
- `.config` - 用户配置（由 gen_config.py 生成）

### 配置选项

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `USBIP_SERVER_PORT` | 服务器监听端口 | 3240 |
| `LOG_LEVEL` | 全局日志级别 | 3 (INF) |
| `MAIN_LOG_LEVEL` | main 模块日志级别 | 3 (INF) |
| `SERVER_LOG_LEVEL` | server 模块日志级别 | 3 (INF) |
| `URB_LOG_LEVEL` | urb 模块日志级别 | 3 (INF) |
| `DEVMGR_LOG_LEVEL` | devmgr 模块日志级别 | 3 (INF) |
| `CONTROL_LOG_LEVEL` | control 模块日志级别 | 4 (DBG) |
| `OSAL_LOG_LEVEL` | osal 模块日志级别 | 3 (INF) |
| `TRANSPORT_LOG_LEVEL` | transport 模块日志级别 | 3 (INF) |
| `DAP_LOG_LEVEL` | HID DAP 模块日志级别 | 3 (INF) |
| `BULK_DAP_LOG_LEVEL` | Bulk DAP 模块日志级别 | 3 (INF) |
| `HID_LOG_LEVEL` | HID 设备日志级别 | 3 (INF) |
| `BULK_LOG_LEVEL` | Bulk 设备日志级别 | 3 (INF) |

### GPIO 配置

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `GPIO_SWCLK` | SWD 时钟引脚 | 17 |
| `GPIO_SWDIO` | SWD 数据引脚 | 27 |
| `GPIO_TCK` | JTAG 时钟引脚 | 17 |
| `GPIO_TMS` | JTAG 模式选择引脚 | 27 |
| `GPIO_TDI` | JTAG 数据输入引脚 | 22 |
| `GPIO_TDO` | JTAG 数据输出引脚 | 23 |

### 日志等级

| 值 | 级别 | 说明 |
|----|------|------|
| 0 | LOG_LEVEL_NONE | 无输出 |
| 1 | LOG_LEVEL_ERR | 错误 |
| 2 | LOG_LEVEL_WRN | 警告 |
| 3 | LOG_LEVEL_INF | 信息 (默认) |
| 4 | LOG_LEVEL_DBG | 调试 |

### 配置步骤

```bash
# 1. 编辑 .config 文件
vim .config

# 示例：修改端口和日志级别
CONFIG_USBIP_SERVER_PORT=3241
CONFIG_USBIP_LOG_LEVEL=4

# 2. 重新生成配置
python scripts/gen_config.py

# 3. 重新编译
cmake -B build -S .
cmake --build build
```

### 重置到默认值

```bash
rm .config
python scripts/gen_config.py
cmake -B build -S .
cmake --build build
```

---

## 运行

```bash
# 加载内核模块
sudo modprobe usbip-core
sudo modprobe usbip-host
sudo modprobe vhci-hcd

# 预先设置 GPIO 输出模式
sudo pinctrl set 17 op dh
sudo pinctrl set 27 op dh

# 启动服务器（默认端口 3240）
sudo build/usbip-server

# 指定端口
sudo build/usbip-server -p 3240

# 查看帮助
./build/usbip-server -h
```

---

## 调试器测试

```bash
# 附加 HID DAP v1 设备
sudo usbip attach -r localhost -b 2-1

# 附加 Bulk DAP v2 设备
sudo usbip attach -r localhost -b 2-2
```

### OpenOCD 烧录（推荐）

```bash
# 编译 OpenOCD（需要树莓派工具链）
./test_flash.sh test/LED.hex openocd bulk

# 或使用 HID 设备
./test_flash.sh test/LED.hex openocd hid
```

### PyOCD 烧录

```bash
./test_flash.sh test/LED.hex pyocd bulk
./test_flash.sh test/LED.hex pyocd hid
```

---

## 协议支持

- [x] OP_REQ_DEVLIST / OP_REP_DEVLIST
- [x] OP_REQ_IMPORT / OP_REP_IMPORT
- [x] USBIP_CMD_SUBMIT / USBIP_RET_SUBMIT
- [x] USBIP_CMD_UNLINK / USBIP_RET_UNLINK
- [x] Control Transfer (EP0)
- [x] Interrupt Transfer
- [x] Bulk Transfer

---

## 许可证

Apache-2.0