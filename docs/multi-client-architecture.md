# USBIP 服务器多客户端支持实现计划

## 背景与目标

### 背景
原 USBIP 服务器采用单客户端架构，一次只能服务一个客户端，设备被占用时其他客户端无法连接其他设备，全局 URB 队列 `g_urb_queue` 被所有连接共享。

### 目标 (已实现)
参考 Linux 内核 USBIP 实现（`../../src/linux-6.12.75/drivers/usb/usbip`），使服务器支持多客户端同时连接不同设备。

**实现状态**: ✅ 已完成 (2026-04-09)

### 参考架构
Linux 内核采用**每设备线程模型**：
- 每个导出的 USB 设备有独立的 TCP 连接
- 每连接有独立的 RX/TX 内核线程
- 设备状态管理：`SDEV_ST_AVAILABLE` → `SDEV_ST_USED`
- 细粒度锁：每设备锁、每 URB 列表锁

---

## 架构设计

### 新架构概览

```
┌─────────────────────────────────────────────────────────────┐
│                    USBIP Server Main                        │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Connection Acceptor Thread (usbip_server_run)      │   │
│  │  - Accepts new connections                          │   │
│  │  - Spawns connection handler threads                │   │
│  └─────────────────────────────────────────────────────┘   │
│                            │                                │
│              ┌─────────────┼─────────────┐                 │
│              ▼             ▼             ▼                 │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐       │
│  │ Connection 1 │ │ Connection 2 │ │ Connection N │       │
│  │  (Device A)  │ │  (Device B)  │ │  (Device C)  │       │
│  │              │ │              │ │              │       │
│  │ ┌──────────┐ │ │ ┌──────────┐ │ │ ┌──────────┐ │       │
│  │ │URB Queue │ │ │ │URB Queue │ │ │ │URB Queue │ │       │
│  │ │(per-conn)│ │ │ │(per-conn)│ │ │ │(per-conn)│ │       │
│  │ └──────────┘ │ │ └──────────┘ │ │ └──────────┘ │       │
│  │  RX Thread   │ │  RX Thread   │ │  RX Thread   │       │
│  │  TX Thread   │ │  TX Thread   │ │  TX Thread   │       │
│  └──────────────┘ └──────────────┘ └──────────────┘       │
└─────────────────────────────────────────────────────────────┘
```

### 设计决策
1. **每连接线程模型**：每个设备连接有独立的 RX/TX 线程（类似内核）
2. **每连接 URB 队列**：取代全局队列，实现完全隔离
3. **连接管理器**：跟踪所有活动连接，支持清理
4. **设备状态扩展**：从二元的 busy/available 改为连接绑定

---

## 数据结构设计

### 1. 连接状态枚举
```c
enum usbip_conn_state {
    CONN_STATE_INIT,        /* Initializing */
    CONN_STATE_ACTIVE,      /* Active URB processing */
    CONN_STATE_CLOSING,     /* Graceful shutdown */
    CONN_STATE_CLOSED       /* Cleaned up */
};
```

### 2. 每连接 URB 队列
```c
struct usbip_conn_urb_queue {
    struct urb_slot slots[USBIP_URB_QUEUE_SIZE];
    int head;
    int tail;
    struct osal_mutex lock;
    struct osal_cond not_empty;
    struct osal_cond not_full;
    int closed;
};
```

### 3. 连接上下文（核心结构）
```c
struct usbip_connection {
    /* Transport context */
    struct usbip_conn_ctx* transport_ctx;

    /* Device binding */
    struct usbip_device_driver* driver;
    char busid[SYSFS_BUS_ID_SIZE];

    /* Connection state */
    enum usbip_conn_state state;
    struct osal_mutex state_lock;

    /* URB processing */
    struct usbip_conn_urb_queue urb_queue;
    struct osal_thread rx_thread;
    struct osal_thread tx_thread;
    volatile int running;

    /* List management for active connections */
    struct usbip_connection* next;
    struct usbip_connection* prev;
};
```

### 4. 连接管理器
```c
struct usbip_conn_manager {
    struct usbip_connection* head;      /* Active connections list */
    struct osal_mutex list_lock;         /* Protects the list */
    int active_count;                    /* Number of active connections */
    int max_connections;                 /* Maximum allowed (configurable) */
};
```

---

## 文件修改计划

### Phase 1: 数据结构定义 (usbip_server.h)

**文件**: `components/usbipd/include/usbip_server.h`

**修改内容**:
1. 添加连接状态枚举 `enum usbip_conn_state`
2. 添加每连接 URB 队列结构 `struct usbip_conn_urb_queue`
3. 添加连接上下文结构 `struct usbip_connection`
4. 添加连接管理器函数声明
5. 添加连接生命周期函数声明

**新增函数声明**:
```c
/* Connection configuration */
#define USBIP_MAX_CONNECTIONS 8

/* Connection manager */
int usbip_conn_manager_init(void);
void usbip_conn_manager_cleanup(void);
int usbip_conn_manager_add(struct usbip_connection* conn);
void usbip_conn_manager_remove(struct usbip_connection* conn);

/* Connection lifecycle */
struct usbip_connection* usbip_connection_create(struct usbip_conn_ctx* ctx);
void usbip_connection_destroy(struct usbip_connection* conn);
int usbip_connection_start(struct usbip_connection* conn,
                           struct usbip_device_driver* driver,
                           const char* busid);
void usbip_connection_stop(struct usbip_connection* conn);
```

### Phase 2: URB 队列重构 (usbip_urb.c)

**文件**: `components/usbipd/src/server/usbip_urb.c`

**修改内容**:
1. 移除全局 `g_urb_queue` 静态变量
2. 修改 URB 队列为每连接实例
3. 添加队列初始化/销毁函数
4. 修改 RX/TX 线程为连接上下文

**关键函数变更**:
```c
/* 旧：全局队列 */
static struct urb_queue g_urb_queue;

/* 新：每连接队列 */
int usbip_urb_queue_init(struct usbip_conn_urb_queue* q);
void usbip_urb_queue_destroy(struct usbip_conn_urb_queue* q);
int usbip_urb_queue_push(struct usbip_conn_urb_queue* q, ...);
int usbip_urb_queue_pop(struct usbip_conn_urb_queue* q, ...);
void usbip_urb_queue_close(struct usbip_conn_urb_queue* q);

/* 线程函数改为接收 connection 参数 */
static void* usbip_conn_rx_thread(void* arg);  /* arg = struct usbip_connection* */
static void* usbip_conn_tx_thread(void* arg);
```

### Phase 3: 设备管理器扩展 (usbip_devmgr.c)

**文件**: `components/usbipd/src/server/usbip_devmgr.c`

**修改内容**:
1. 扩展设备信息结构，添加连接绑定
2. 添加设备绑定/解绑函数
3. 修改 `usbip_is_device_busy()` 逻辑

**新增代码**:
```c
struct usbip_device_info {
    struct usbip_usb_device udev;
    struct usbip_connection* owner;     /* NULL if available */
    enum {
        DEV_STATE_AVAILABLE = 0,
        DEV_STATE_EXPORTED,
        DEV_STATE_BUSY
    } state;
    struct osal_mutex lock;
};

int usbip_bind_device(const char* busid, struct usbip_connection* conn);
void usbip_unbind_device(const char* busid);
struct usbip_connection* usbip_get_device_owner(const char* busid);
```

### Phase 4: 服务器核心重构 (usbip_server.c)

**文件**: `components/usbipd/src/server/usbip_server.c`

**修改内容**:
1. 重写 `usbip_server_run()` 为连接接受循环
2. 修改 `usbip_server_handle_import_req()` 为非阻塞
3. 移除 `usbip_urb_loop()` 调用，改为启动连接线程
4. 修改设备导出函数签名

**关键变更**:
```c
/* 新 server run 逻辑 */
int usbip_server_run(void)
{
    struct usbip_conn_ctx* transport_ctx;
    struct usbip_connection* conn;

    usbip_conn_manager_init();

    while (s_running) {
        transport_ctx = transport_accept();
        if (!transport_ctx) continue;

        conn = usbip_connection_create(transport_ctx);
        if (!conn) {
            transport_close(transport_ctx);
            continue;
        }

        /* Handle protocol exchange */
        if (usbip_server_handle_protocol(conn) < 0) {
            usbip_connection_destroy(conn);
            continue;
        }

        /* Start URB processing threads */
        usbip_connection_start(conn, conn->driver, conn->busid);
    }

    usbip_conn_manager_cleanup();
    return 0;
}
```

### Phase 5: 新文件 - 连接管理 (usbip_conn.c)

**新文件**: `components/usbipd/src/server/usbip_conn.c`

**功能**: 连接生命周期管理

**实现内容**:
```c
static struct usbip_conn_manager g_conn_manager;

int usbip_conn_manager_init(void);
void usbip_conn_manager_cleanup(void);
int usbip_conn_manager_add(struct usbip_connection* conn);
void usbip_conn_manager_remove(struct usbip_connection* conn);

struct usbip_connection* usbip_connection_create(struct usbip_conn_ctx* ctx);
void usbip_connection_destroy(struct usbip_connection* conn);
int usbip_connection_start(struct usbip_connection* conn, ...);
void usbip_connection_stop(struct usbip_connection* conn);
```

### Phase 6: 设备驱动更新

**文件**:
- `components/usbipd/src/hid_dap.c`
- `components/usbipd/src/bulk_dap.c`

**修改内容**:
```c
/* 修改 export_device 签名 */
static int vdap_export_device(struct usbip_device_driver* driver,
                              const char* busid,
                              struct usbip_connection* conn);  /* 原：usbip_conn_ctx* */

/* 存储连接引用而非传输上下文 */
vdap.conn = conn;  /* 原：vdap.ctx = ctx */
```

### Phase 7: CMakeLists.txt 更新

**文件**: `components/usbipd/CMakeLists.txt`

**修改**: 添加新源文件 `src/server/usbip_conn.c`

---

## 同步策略

### 锁层级（死锁预防）
按此顺序获取锁：
1. `g_conn_manager.list_lock`
2. `device_registry[i].lock`
3. `conn->state_lock`
4. `queue->lock`

### 关键同步场景

**场景 1: URB 处理中连接断开**
```c
/* TX 线程 */
while (conn->running) {
    if (usbip_urb_queue_pop(&conn->urb_queue, ...) < 0) {
        break;  /* 队列关闭，正常退出 */
    }
    /* 处理 URB... */
}

/* 主线程关闭 */
void usbip_connection_stop(struct usbip_connection* conn) {
    conn->running = 0;           /* 通知线程停止 */
    usbip_urb_queue_close(&conn->urb_queue);  /* 唤醒阻塞线程 */
    osal_thread_join(&conn->rx_thread);       /* 等待完成 */
    osal_thread_join(&conn->tx_thread);
}
```

**场景 2: 设备忙检查**
```c
int usbip_bind_device(const char* busid, struct usbip_connection* conn) {
    osal_mutex_lock(&device->lock);
    if (device->state != DEV_STATE_AVAILABLE) {
        osal_mutex_unlock(&device->lock);
        return -EBUSY;
    }
    device->owner = conn;
    device->state = DEV_STATE_EXPORTED;
    osal_mutex_unlock(&device->lock);
    return 0;
}
```

---

## 实现顺序

| 阶段 | 任务 | 预计时间 | 依赖 | 状态 |
|------|------|---------|------|------|
| 1 | 更新 `usbip_server.h` 数据结构 | 2-4h | - | ✅ 已完成 |
| 2 | 实现 `usbip_conn.c` 连接管理 | 4-6h | Phase 1 | ✅ 已完成 |
| 3 | 重构 `usbip_urb.c` 每连接队列 | 6-8h | Phase 1 | ✅ 已完成 |
| 4 | 更新 `usbip_devmgr.c` 设备绑定 | 4-6h | Phase 1 | ✅ 已完成 |
| 5 | 重构 `usbip_server.c` 主循环 | 6-8h | Phase 2,3,4 | ✅ 已完成 |
| 6 | 更新设备驱动 | 2-4h | Phase 4 | ✅ 已完成 |
| 7 | 测试验证 | 4-6h | Phase 6 | ✅ 已完成 |

---

## 验证计划

### 功能测试 (已完成)
- ✅ **多设备连接**: 同时连接 hid_dap (2-1) 和 bulk_dap (2-2)
- ✅ **设备隔离**: 每个连接有独立的 URB 队列，完全隔离
- ✅ **断开处理**: 单个连接断开不影响其他连接
- ✅ **资源清理**: 连接关闭后线程、队列、资源正确释放

### 性能测试 (已完成)
- ✅ **并发性能**: 每设备独立线程处理，无全局锁竞争
- ✅ **内存使用**: 每连接静态 URB 队列，内存占用可预测

### 回归测试 (已完成)
- ✅ 单客户端模式正常工作
- ✅ OpenOCD/PyOCD 烧录功能正常

## 实现总结

### 主要变更

| 组件 | 变更内容 |
|------|----------|
| `usbip_server.h` | 添加连接状态枚举、URB 队列结构、连接上下文 |
| `usbip_conn.c` | 新建：连接管理器、生命周期管理、RX/Processor 双线程 |
| `usbip_urb.c` | 添加每连接 URB 队列操作函数 |
| `usbip_devmgr.c` | 添加设备绑定/解绑接口 |
| `usbip_server.c` | 重写主循环为连接接受器，非阻塞 IMPORT |
| `hid_dap.c` | 更新为 `usbip_connection*` 驱动接口 |
| `bulk_dap.c` | 更新为 `usbip_connection*` 驱动接口 |
| `CMakeLists.txt` | 添加 `-u` 符号保留和 `--gc-sections` |

---

## 关键文件清单

| 文件路径 | 修改类型 | 状态 | 说明 |
|----------|---------|------|------|
| `components/usbipd/include/usbip_server.h` | 修改 | ✅ 完成 | 添加连接数据结构、URB 队列接口 |
| `components/usbipd/src/server/usbip_conn.c` | 新建 | ✅ 完成 | 连接管理实现、双线程模型 |
| `components/usbipd/src/server/usbip_urb.c` | 修改 | ✅ 完成 | 每连接 URB 队列操作函数 |
| `components/usbipd/src/server/usbip_devmgr.c` | 修改 | ✅ 完成 | 设备绑定/解绑接口 |
| `components/usbipd/src/server/usbip_server.c` | 修改 | ✅ 完成 | 主循环重构为连接接受器 |
| `components/usbipd/src/hid_dap.c` | 修改 | ✅ 完成 | 设备驱动更新为 `usbip_connection*` 接口 |
| `components/usbipd/src/bulk_dap.c` | 修改 | ✅ 完成 | 设备驱动更新为 `usbip_connection*` 接口 |
| `components/usbipd/src/device/usbip_bulk.c` | 修改 | ✅ 完成 | 同步更新驱动接口 |
| `components/usbipd/CMakeLists.txt` | 修改 | ✅ 完成 | 添加 `-u` 符号保留选项 |
| `CMakeLists.txt` | 修改 | ✅ 完成 | 添加 `--gc-sections` 优化 |

## 编译运行

```bash
# 配置和编译
cmake -B build -S .
cmake --build build

# 运行服务器
sudo ./build/usbip-server

# 连接设备 (多客户端)
sudo usbip attach -r localhost -b 2-1  # HID DAP
sudo usbip attach -r localhost -b 2-2  # Bulk DAP
```
