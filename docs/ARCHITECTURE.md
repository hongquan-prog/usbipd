# USBIP Server 架构文档

## 目录
- [概述](#概述)
- [系统架构](#系统架构)
- [核心模块详解](#核心模块详解)
- [数据流程](#数据流程)
- [关键设计模式](#关键设计模式)
- [扩展指南](#扩展指南)

---

## 概述

这是一个模块化的 USBIP 服务器实现，采用清晰的三层架构设计，支持多种虚拟 USB 设备。服务器运行在用户空间，通过 USBIP 协议将虚拟 USB 设备导出给远程客户端使用。

### 主要特性

- 模块化设计，各层职责明确
- 传输层抽象，支持 TCP/串口等多种传输方式
- 设备驱动框架，易于扩展新设备
- 静态内存管理，适用于嵌入式环境
- 线程安全的 URB 队列处理
- 带颜色的日志系统

### 支持的设备

| 设备 | BusID | 说明 |
|------|-------|------|
| CMSIS-DAP v2 Bulk | 2-2 | 主要调试设备 |
| CMSIS-DAP v1 HID | 2-1 | HID 调试设备 |
| HID 键盘 | 1-1 | 通用 HID 设备 |

---

## 系统架构

### 整体架构图

```
┌─────────────────────────────────────────────────────────────┐
│                        客户端 (usbip)                          │
└──────────────────────────┬──────────────────────────────────┘
                           │ TCP/3240
┌──────────────────────────┴──────────────────────────────────┐
│  ┌──────────────────────────────────────────────────────┐   │
│  │         传输层 (Transport Layer)               │   │
│  │  - transport_tcp.c (TCP 实现，自动注册)    │   │
│  └──────────────────────┬───────────────────────────┘   │
│                         │                              │
│  ┌──────────────────────┴───────────────────────────┐   │
│  │       USBIP 协议层 (Protocol Layer)            │   │
│  │  - usbip_protocol.c (协议编解码)             │   │
│  │  - usbip_server.c (连接管理)                 │   │
│  │  - usbip_urb.c (URB 队列处理)               │   │
│  └──────────────────────┬───────────────────────────┘   │
│                         │                              │
│  ┌──────────────────────┴───────────────────────────┐   │
│  │      设备管理层 (Device Manager Layer)         │   │
│  │  - usbip_devmgr.c (驱动注册/管理)         │   │
│  └──────────────────────┬───────────────────────────┘   │
│                         │                              │
│  ┌──────────────────────┴───────────────────────────┐   │
│  │        设备驱动层 (Device Driver Layer)          │   │
│  │  - hid_dap.c (CMSIS-DAP v1 HID)           │   │
│  │  - bulk_dap.c (CMSIS-DAP v2 Bulk)     │   │
│  │  - usbip_hid.c (HID 基类)               │   │
│  │  - usbip_bulk.c (Bulk 基类)              │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### 分层说明

#### 1. 传输层 (Transport Layer)

**职责**: 提供跨平台的传输抽象，支持不同的传输方式。使用全局单例模式，通过 constructor 自动注册。

**文件结构**:
```
src/
├── hal/usbip_transport.h      # 传输层接口定义
├── hal/usbip_transport.c      # 传输层核心：全局实例 + wrapper 函数
└── transport_tcp.c            # TCP 传输实现
```

**核心接口**: `struct usbip_transport`
- `listen()` - 开始监听
- `accept()` - 接受连接
- `recv()` - 接收数据
- `send()` - 发送数据
- `close()` - 关闭连接
- `destroy()` - 销毁实例
- `get_poll_fd()` - 获取 poll 文件描述符

**Wrapper 函数** (在 `usbip_transport.c` 中):
- `transport_listen()` - 启动监听
- `transport_accept()` - 接受连接
- `transport_recv()` - 接收数据
- `transport_send()` - 发送数据
- `transport_close()` - 关闭连接
- `transport_destroy()` - 销毁实例
- `transport_get_poll_fd()` - 获取 poll fd
- `transport_set_global()` - 设置全局实例 (内部使用)

**自动注册机制**:
```c
/* transport_tcp.c */
static void __attribute__((constructor)) tcp_transport_register(void)
{
    struct usbip_transport* trans = tcp_transport_create();
    if (trans)
    {
        transport_set_global(trans);
    }
}
```

**当前实现**:
- TCP 传输 (`transport_tcp.c`)
  - 使用 `MSG_WAITALL` 确保完整数据接收
  - `TCP_NODELAY` 禁用 Nagle 算法减少延迟
  - `SO_KEEPALIVE` 保持连接

#### 2. USBIP 协议层 (Protocol Layer)

**职责**: 实现 USBIP 协议的编解码和连接管理。

**核心功能**:
- 协议字节序转换 (`usbip_pack_*` 函数)
- 设备枚举请求处理 (`OP_REQ_DEVLIST`)
- 设备导入请求处理 (`OP_REQ_IMPORT`)
- URB 收发处理

**协议常量**:
```c
#define USBIP_VERSION 0x0111

/* 操作码 */
#define OP_REQ_DEVLIST  0x8005  /* 获取设备列表 */
#define OP_REP_DEVLIST  0x0005
#define OP_REQ_IMPORT   0x8003  /* 导入设备 */
#define OP_REP_IMPORT   0x0003

/* URB 命令 */
#define USBIP_CMD_SUBMIT  0x0001
#define USBIP_CMD_UNLINK  0x0002
#define USBIP_RET_SUBMIT  0x0003
#define USBIP_RET_UNLINK  0x0004
```

**核心数据结构**:
```c
/* 操作公共头 (8 bytes) */
struct op_common {
    uint16_t version;
    uint16_t code;
    uint32_t status;
};

/* URB 完整头 (48 bytes) */
struct usbip_header {
    struct usbip_header_basic base;
    union {
        struct usbip_header_cmd_submit cmd_submit;
        struct usbip_header_ret_submit ret_submit;
        struct usbip_header_cmd_unlink cmd_unlink;
        struct usbip_header_ret_unlink ret_unlink;
    } u;
};
```

#### 3. 设备管理层 (Device Manager Layer)

**职责**: 管理设备驱动注册、设备状态跟踪。

**核心接口**: `struct usbip_device_driver`

```c
struct usbip_device_driver {
    const char* name;

    /* 设备枚举 */
    int (*get_device_list)(struct usbip_device_driver* driver,
                          struct usbip_usb_device** devices, int* count);
    const struct usbip_usb_device* (*get_device)(struct usbip_device_driver* driver,
                                                 const char* busid);

    /* 设备导入/导出 */
    int (*export_device)(struct usbip_device_driver* driver, const char* busid,
                         struct usbip_conn_ctx* ctx);
    int (*unexport_device)(struct usbip_device_driver* driver, const char* busid);

    /* URB 处理 */
    int (*handle_urb)(struct usbip_device_driver* driver,
                      const struct usbip_header* urb_cmd,
                      struct usbip_header* urb_ret,
                      void** data_out, size_t* data_len,
                      const void* urb_data, size_t urb_data_len);

    /* 生命周期 */
    int (*init)(struct usbip_device_driver* driver);
    void (*cleanup)(struct usbip_device_driver* driver);
};
```

**管理接口**:
- `usbip_register_driver()` - 注册驱动
- `usbip_unregister_driver()` - 注销驱动
- `usbip_get_first_driver()` / `usbip_get_next_driver()` - 驱动迭代
- `usbip_set_device_busy()` / `usbip_set_device_available()` - 设备状态管理

#### 4. 设备驱动层 (Device Driver Layer)

**职责**: 实现具体的虚拟 USB 设备功能。

#### 5. 日志系统 (Log System)

**职责**: 提供统一的日志输出接口，支持彩色输出。

**文件**: `include/hal/usbip_log.h`

**日志等级**:
```c
#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERR  1
#define LOG_LEVEL_WRN  2
#define LOG_LEVEL_INF  3
#define LOG_LEVEL_DBG  4
```

**颜色配置**:
| 等级 | 颜色 | 输出 |
|------|------|------|
| LOG_ERR | 红色 | stderr |
| LOG_WRN | 黄色 | stderr |
| LOG_INF | 绿色 | stdout |
| LOG_DBG | 默认 | stdout |

**使用方式**:
```c
/* 注册模块 */
LOG_MODULE_REGISTER(my_module, LOG_LEVEL_DBG);

/* 使用日志宏 */
LOG_ERR("Error occurred: %d", err);
LOG_WRN("Warning: %s", msg);
LOG_INF("Info: %s", msg);
LOG_DBG("Debug: %s", msg);
```

**颜色控制**:
```c
/* 禁用颜色 */
#define LOG_USE_COLOR 0
```

#### 6. OSAL 抽象层 (OS Abstraction Layer)

**职责**: 提供可移植的操作系统原语接口，支持不同平台实现。

**文件结构**:
```
src/hal/
├── usbip_osal.h     # OSAL 接口定义
├── usbip_osal.c     # OSAL 核心：wrapper 函数
└── usbip_mempool.c  # 内存池实现

src/
└── osal_posix.c     # POSIX 平台实现
```

**接口结构**:
```c
typedef struct osal_ops {
    /* 互斥锁 */
    int (*mutex_init)(void* handle);
    int (*mutex_lock)(void* handle);
    int (*mutex_unlock)(void* handle);
    void (*mutex_destroy)(void* handle);

    /* 条件变量 */
    int (*cond_init)(void* handle);
    int (*cond_wait)(void* cond, void* mutex);
    int (*cond_timedwait)(void* cond, void* mutex, uint32_t timeout_ms);
    int (*cond_signal)(void* cond);
    int (*cond_broadcast)(void* cond);
    void (*cond_destroy)(void* handle);

    /* 线程 */
    int (*thread_create)(void* handle, void* (*func)(void*), void* arg, size_t stack_size, int priority);
    int (*thread_join)(void* handle);
    int (*thread_detach)(void* handle);

    /* 时间 */
    uint32_t (*get_time_ms)(void);
    void (*sleep_ms)(uint32_t ms);

    /* 内存 */
    void* (*malloc)(size_t size);
    void (*free)(void* ptr);

    const char* name;
} osal_ops_t;
```

**统一包装函数**:
- `osal_mutex_init/destroy/lock/unlock`
- `osal_cond_init/destroy/wait/signal/broadcast`
- `osal_thread_create/join/detach`
- `osal_get_time_ms/sleep_ms`
- `osal_malloc/free`
- `osal_mempool_init/alloc/free/destroy`

#### 7. 调试探针组件 (Debug Probe Components)

**职责**: 提供 SWD/JTAG 调试协议实现和 GPIO 控制。

**组件结构**:
```
components/debug_probe/
├── debug_gpio.c      # GPIO bit-banging 实现
├── debug_gpio.h      # GPIO 引脚定义
├── swd.c             # SWD 协议实现
├── DAP/
│   ├── Include/DAP.h      # DAP 公共定义
│   ├── Source/
│   │   ├── DAP.c          # DAP 核心实现
│   │   ├── SW_DP.c        # SWD 调试端口
│   │   ├── JTAG_DP.c      # JTAG 调试端口
│   │   └── DAP_vendor.c   # 厂商自定义命令
│   └── Config/
│       └── DAP_config.h   # DAP 配置
└── include/
    └── debug_probe.h      # 公共头文件
```

**关键接口**:

```c
/* GPIO 控制 */
int debug_gpio_init(void);
void debug_gpio_set(int pin, int value);
int debug_gpio_get(int pin);
void debug_gpio_set_mode(int pin, int mode);

/* SWD 协议 */
int swd_init(void);
int swd_write(uint32_t swdio, int swclk);
int swd_read(uint32_t* swdio);
int swd_cycle_clock(void);
int swd_write_bits(uint32_t data, int bits);
int swd_read_bits(uint32_t* data, int bits);

/* DAP 命令 */
int DAP_Init(void);
int DAP_Connect(uint32_t mode);
int DAP_SWJ_Sequence(uint32_t count, const uint8_t* data);
int DAP_SWDP_Transfer(uint8_t request, uint16_t data, uint32_t* response);
```

---

## 核心模块详解

### 1. 传输层 - TCP 实现

**文件**: `src/transport_tcp.c`

**设计要点**:

1. **连接上下文** (`struct tcp_conn_priv`)
   - 封装 socket 文件描述符
   - 应用层不直接操作底层句柄

2. **数据接收** (`tcp_recv()`)
   - 使用 `MSG_WAITALL` 标志确保接收完整数据
   - 处理 `EINTR` 信号中断重试
   - 循环接收直到满足长度要求

3. **数据发送** (`tcp_send()`)
   - 使用 `MSG_NOSIGNAL` 避免 `SIGPIPE`
   - 循环发送直到全部数据发出

4. **自动注册** (`tcp_transport_register()`)
   - 使用 `__attribute__((constructor))` 在程序启动时自动调用
   - 创建 transport 实例并注册为全局实例

**Socket 配置**:
```c
/* TCP_NODELAY - 禁用 Nagle 算法，减少延迟 */
setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

/* SO_KEEPALIVE - 保持连接 */
setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
```

### 2. 协议层 - 服务器核心

**文件**: `src/server/usbip_server.c`

**服务器启动流程**:

```
main()
├── transport_listen()           # 通过 wrapper 启动监听
├── usbip_register_driver()     # 注册设备驱动
├── usbip_server_init()          # 初始化服务器
│   ├── setup_signals()          # 设置信号处理
│   └── transport_listen()       # 开始监听
└── usbip_server_run()           # 运行主循环
    ├── poll()                   # 等待连接
    ├── transport_accept()       # 接受连接
    └── usbip_server_handle_connection()
        ├── usbip_recv_op_common()   # 接收操作头
        ├── usbip_server_handle_devlist()  # 处理设备列表
        └── usbip_server_handle_import_req()  # 处理设备导入
            └── usbip_urb_loop()      # 进入 URB 循环
```

**连接处理**:

1. **设备列表请求 (`OP_REQ_DEVLIST`)
   - 遍历所有已注册驱动
   - 收集设备信息
   - 发送设备列表给客户端

2. **设备导入请求 (`OP_REQ_IMPORT`)
   - 接收 busid
   - 查找设备
   - 检查设备状态
   - 导出设备
   - 进入 URB 处理循环

### 3. URB 处理模块

**文件**: `src/server/usbip_urb.c`

**双线程架构**:

```
┌─────────────────────────────────────────────────────────┐
│           接收线程 (Receiver Thread)              │
│  - usbip_recv_header()                      │
│  - 接收 OUT 数据                           │
│  - urb_queue_push()                          │
└───────────────┬─────────────────────────────────┘
                │
                ▼
        ┌───────────────┐
        │  URB 队列     │
        │  (线程安全)    │
        └───────┬───────┘
                │
                ▼
┌─────────────────────────────────────────────────────────┐
│         处理线程 (Processor Thread)             │
│  - urb_queue_pop()                           │
│  - driver->handle_urb()                    │
│  - urb_send_reply()                          │
└─────────────────────────────────────────────────────────┘
```

**静态 URB 队列**:

```c
#define USBIP_URB_QUEUE_SIZE  8    /* 队列槽位数 */
#define USBIP_URB_DATA_MAX_SIZE 256  /* 每个 URB 最大数据 */

struct urb_slot {
    struct usbip_header header;
    uint8_t data[USBIP_URB_DATA_MAX_SIZE];
    size_t data_len;
    int valid;
};

struct urb_queue {
    struct urb_slot slots[USBIP_URB_QUEUE_SIZE];
    int head;
    int tail;
    struct osal_mutex lock;
    struct osal_cond not_empty;
    struct osal_cond not_full;
    int closed;
};
```

**队列操作**:

- `urb_queue_push()` - 入队 URB
  - 等待队列非满条件变量
  - 复制数据到静态槽位
  - 通知非空条件变量

- `urb_queue_pop()` - 出队 URB
  - 等待队列非空条件变量
  - 从静态槽位复制数据
  - 通知非满条件变量

**URB 处理循环**:

```c
usbip_urb_loop()
├── urb_queue_init()          # 初始化队列
├── osal_thread_create()       # 启动处理线程
├── 接收线程循环:
│   ├── usbip_recv_header()
│   ├── 接收 OUT 数据
│   └── urb_queue_push()
└── 处理线程:
    ├── urb_queue_pop()
    ├── driver->handle_urb()
    └── urb_send_reply()
```

### 4. 设备管理器

**文件**: `src/server/usbip_devmgr.c`

**驱动注册表**:
- 最多支持 16 个驱动
- 注册时调用 `driver->init()`
- 注销时调用 `driver->cleanup()`

**设备忙碌状态表**:
- 最多跟踪 32 个设备
- 使用 busid 作为键
- 防止设备被重复导入

---

## 数据流程

### 1. 设备枚举流程

```
客户端                    服务器
  │                         │
  │  OP_REQ_DEVLIST       │
  ├────────────────────────>│
  │                         │
  │  OP_REP_DEVLIST        │
  │  + 设备数量            │
  │  + 设备描述 1           │
  │  + 接口描述 1           │
  │  + 设备描述 2           │
  │  + 接口描述 2           │
  │  ...                    │
  <────────────────────────┤
  │                         │
```

### 2. 设备导入流程

```
客户端                    服务器
  │                         │
  │  OP_REQ_IMPORT          │
  │  + busid                │
  ├────────────────────────>│
  │                         │
  │  OP_REP_IMPORT          │
  │  + 设备描述             │
  <────────────────────────┤
  │                         │
  │      URB 交换          │
  │  (USBIP_CMD_SUBMIT)     │
  ├────────────────────────>│
  │                         │
  │  (USBIP_RET_SUBMIT)     │
  <────────────────────────┤
  │      (循环)               │
```

### 3. URB 处理流程 (OUT 传输)

```
客户端                    服务器
  │                         │
  │  USBIP_CMD_SUBMIT      │
  │  + URB 头              │
  │  + OUT 数据             │
  ├────────────────────────>│
  │                         │
  │  ┌─────────────────┐   │
  │  │  接收线程      │   │
  │  │  urb_queue_push()│  │
  │  └────────┬────────┘   │
  │           │            │
  │           ▼            │
  │  ┌─────────────────┐   │
  │  │   URB 队列      │   │
  │  └────────┬────────┘   │
  │           │            │
  │           ▼            │
  │  ┌─────────────────┐   │
  │  │  处理线程       │   │
  │  │  handle_urb()  │   │
  │  └─────────────────┘   │
  │                         │
  │  USBIP_RET_SUBMIT      │
  <────────────────────────┤
  │                         │
```

### 4. URB 处理流程 (IN 传输)

```
客户端                    服务器
  │                         │
  │  USBIP_CMD_SUBMIT      │
  │  + URB 头 (IN)        │
  ├────────────────────────>│
  │                         │
  │  ┌─────────────────┐   │
  │  │  接收线程      │   │
  │  │  urb_queue_push()│  │
  │  └────────┬────────┘   │
  │           │            │
  │           ▼            │
  │  ┌─────────────────┐   │
  │  │   URB 队列      │   │
  │  └────────┬────────┘   │
  │           │            │
  │           ▼            │
  │  ┌─────────────────┐   │
  │  │  处理线程       │   │
  │  │  handle_urb()  │   │
  │  │  生成 IN 数据   │   │
  │  └─────────────────┘   │
  │                         │
  │  USBIP_RET_SUBMIT      │
  │  + IN 数据             │
  <────────────────────────┤
  │                         │
```

---

## 关键设计模式

### 1. 单例模式 (Singleton Pattern)

**应用**: 传输层全局实例

```c
/* 内部全局变量 */
static struct usbip_transport* g_transport = NULL;

/* 内部设置函数 */
void transport_set_global(struct usbip_transport* trans)
{
    g_transport = trans;
}
```

通过 constructor 在程序启动时自动创建和注册。

### 2. 策略模式 (Strategy Pattern)

**应用**: 传输层抽象

```c
struct usbip_transport {
    int (*listen)(struct usbip_transport* trans, uint16_t port);
    struct usbip_header_basic base;
    // ... 其他函数指针
};
```

通过函数指针实现不同传输策略的动态切换。

### 3. 模板方法模式 (Template Method Pattern)

**应用**: 设备驱动接口

设备驱动实现统一的接口，服务器核心按照固定流程调用驱动方法。

### 4. 生产者-消费者模式 (Producer-Consumer Pattern)

**应用**: URB 双线程队列

- 接收线程 = 生产者
- 处理线程 = 消费者
- 条件变量同步

### 5. 注册表模式 (Registry Pattern)

**应用**: 设备驱动管理

```c
static struct usbip_device_driver* driver_registry[MAX_DRIVERS];
static int driver_count = 0;
```

---

## 扩展指南

### 添加新的传输方式

1. 创建新传输实现文件 `src/transport_xxx.c`:

```c
#include "hal/usbip_transport.h"

struct xxx_transport_priv {
    /* 私有数据 */
};

struct xxx_conn_priv {
    /* 连接私有数据 */
};

static int xxx_listen(struct usbip_transport* trans, uint16_t port)
{
    /* 实现监听 */
}

static struct usbip_conn_ctx* xxx_accept(struct usbip_transport* trans)
{
    /* 实现接受连接 */
}

/* 实现其他接口... */

static struct usbip_transport* xxx_transport_create(void)
{
    struct usbip_transport* trans;
    struct xxx_transport_priv* priv;

    trans = calloc(1, sizeof(*trans));
    priv = calloc(1, sizeof(*priv));

    trans->priv = priv;
    trans->listen = xxx_listen;
    trans->accept = xxx_accept;
    /* 设置其他函数指针... */

    return trans;
}

/* 自动注册 */
extern void transport_set_global(struct usbip_transport* trans);

static void __attribute__((constructor)) xxx_transport_register(void)
{
    struct usbip_transport* trans = xxx_transport_create();
    if (trans)
    {
        transport_set_global(trans);
    }
}
```

2. 传输层已自动注册，无需修改 main.c

### 添加新的设备驱动

1. 创建设备驱动文件 `src/device/usbip_xxx.c`:

```c
#include "usbip_devmgr.h"

/* 设备描述 */
static struct usbip_usb_device xxx_device = {
    .busid = "3-1",
    .idVendor = 0x1234,
    .idProduct = 0x5678,
    /* ... */
};

static int xxx_get_device_count(struct usbip_device_driver* driver)
{
    (void)driver;
    return 1;
}

static int xxx_get_device_by_index(struct usbip_device_driver* driver, int index,
                                   struct usbip_usb_device* device)
{
    (void)driver;
    if (index != 0)
    {
        return -1;
    }
    memcpy(device, &xxx_device, sizeof(*device));
    return 0;
}

static const struct usbip_usb_device* xxx_get_device(
    struct usbip_device_driver* driver, const char* busid)
{
    if (strcmp(busid, xxx_device.busid) == 0) {
        return &xxx_device;
    }
    return NULL;
}

static int xxx_export_device(struct usbip_device_driver* driver,
                            const char* busid, struct usbip_conn_ctx* ctx)
{
    usbip_set_device_busy(busid);
    return 0;
}

static int xxx_unexport_device(struct usbip_device_driver* driver,
                              const char* busid)
{
    usbip_set_device_available(busid);
    return 0;
}

static int xxx_handle_urb(struct usbip_device_driver* driver,
                         const struct usbip_header* urb_cmd,
                         struct usbip_header* urb_ret,
                         void** data_out, size_t* data_len,
                         const void* urb_data, size_t urb_data_len)
{
    /* 处理 URB */
    urb_ret->base.command = USBIP_RET_SUBMIT;
    urb_ret->base.seqnum = urb_cmd->base.seqnum;
    urb_ret->base.devid = urb_cmd->base.devid;
    urb_ret->base.direction = urb_cmd->base.direction;
    urb_ret->base.ep = urb_cmd->base.ep;
    urb_ret->u.ret_submit.status = 0;
    urb_ret->u.ret_submit.actual_length = 0;

    return 1; /* 需要发送响应 */
}

static int xxx_init(struct usbip_device_driver* driver)
{
    return 0;
}

static void xxx_cleanup(struct usbip_device_driver* driver)
{
}

struct usbip_device_driver virtual_xxx_driver = {
    .name = "virtual_xxx",
    .get_device_count = xxx_get_device_count,
    .get_device_by_index = xxx_get_device_by_index,
    .get_device = xxx_get_device,
    .export_device = xxx_export_device,
    .unexport_device = xxx_unexport_device,
    .handle_urb = xxx_handle_urb,
    .init = xxx_init,
    .cleanup = xxx_cleanup,
};
```

2. 在 `main.c` 中注册驱动:

```c
extern struct usbip_device_driver virtual_xxx_driver;

/* 在 main() 中 */
usbip_register_driver(&virtual_xxx_driver);
```

### 内存管理注意事项

1. **静态内存优先**: 项目设计考虑了嵌入式环境，优先使用静态分配。

2. **URB 数据缓冲区**:
   - 驱动分配 `data_out`，框架释放
   - 使用 `osal_malloc()` 分配，`osal_free()` 释放

3. **设备列表**:
   - 驱动分配设备数组，框架释放
   - 使用 `osal_malloc()` / `realloc()` 分配，`osal_free()` 释放

### 线程安全

1. **URB 队列**:
   - 使用 `osal_mutex` 保护
   - 使用 `osal_cond` 同步

2. **设备状态**:
   - 单线程修改，无需锁保护

3. **驱动注册表**:
   - 初始化时注册，运行时只读

---

## 附录

### 配置常量

```c
/* 服务器配置 */
#define USBIP_URB_QUEUE_SIZE    8
#define USBIP_URB_DATA_MAX_SIZE  256

/* 设备管理 */
#define MAX_DRIVERS        16
#define MAX_BUSY_DEVICES   32
#define SYSFS_PATH_MAX    256
#define SYSFS_BUS_ID_SIZE  32
```

### USB 速度定义

```c
#define USB_SPEED_UNKNOWN   0
#define USB_SPEED_LOW       1
#define USB_SPEED_FULL      2
#define USB_SPEED_HIGH      3
#define USB_SPEED_WIRELESS 4
#define USB_SPEED_SUPER     5
#define USB_SPEED_SUPER_PLUS 6
```

### USBIP 状态码

```c
#define ST_OK        0x00  /* 成功 */
#define ST_NA        0x01  /* 不可用 */
#define ST_DEV_BUSY  0x02  /* 设备忙 */
#define ST_DEV_ERR   0x03  /* 设备错误 */
#define ST_NODEV     0x04  /* 无设备 */
#define ST_ERROR     0x05  /* 通用错误 */
```

### 编译和运行

参见 `CLAUDE.md` 获取详细的编译和运行说明。

---

**文档版本**: 1.4
**最后更新**: 2026-03-25