# Blackmagic GDB Server 移植方案

## 1. 架构概述

### 1.1 目标架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                         GDB Server (port 2000)                       │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────────┐  │
│  │  gdb_main   │  │ gdb_packet  │  │         gdb_if              │  │
│  │  (RSP协议)   │  │  (包处理)    │  │    (TCP server)             │  │
│  └──────┬──────┘  └──────┬──────┘  └─────────────┬───────────────┘  │
└─────────┼────────────────┼──────────────────────┼──────────────────┘
          │                │                      │
          ▼                ▼                      ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    target_adapter (新建)                             │
│  ┌─────────────────────────────────────────────────────────────────┐│
│  │  target_s 结构体实现 (Blackmagic 兼容接口)                        ││
│  │                                                                 ││
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐   ││
│  │  │  Flash 操作   │  │  调试操作     │  │    辅助功能          │   ││
│  │  │              │  │              │  │                     │   ││
│  │  │ - flash_erase │  │ - mem_read   │  │ - reset             │   ││
│  │  │ - flash_write │  │ - mem_write  │  │ - halt/resume       │   ││
│  │  │ - flash_compl │  │ - reg_read   │  │ - break/watch       │   ││
│  │  └──────┬───────┘  └──────┬───────┘  └──────────┬──────────┘   ││
│  └─────────┼────────────────┼─────────────────────┼──────────────┘│
└────────────┼────────────────┼─────────────────────┼─────────────────┘
             │                │                     │
             ▼                └──────────┬──────────┘
┌────────────────────┐                   │
│   Program 组件      │                   │
│  (TargetFlash)      │                   │
│                     │                   │
│  - Flash 算法加载    │                   │
│  - 擦除/编程         │                   │
└────────────────────┘                   │
                                         ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      debug_probe (DAP 驱动)                          │
│                                                                     │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────┐  │
│  │   SWD_Transfer  │  │   DAP_Execute   │  │    GPIO Control     │  │
│  │   (SW_DP.c)     │  │   (DAP.c)       │  │   (debug_gpio.c)    │  │
│  │                 │  │                 │  │                     │  │
│  │  - 底层 SWD 传输 │  │  - DAP 命令执行  │  │  - SWCLK/SWDIO 控制  │  │
│  │  - 读写 DP/AP   │  │  - 批量传输     │  │  - 复位引脚控制      │  │
│  └─────────────────┘  └─────────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

### 1.2 组件职责划分

| 功能 | 实现组件 | 调用接口 | 说明 |
|------|----------|----------|------|
| Flash 烧录 | **Program** | `TargetFlash::flash_erase/program` | 使用 Keil FLM 算法 |
| SWD 传输 | **debug_probe** | `SWD_Transfer(request, data)` | 底层 SWD 读写 |
| DAP 命令 | **debug_probe** | `DAP_ExecuteCommand()` | 执行 DAP 命令序列 |
| 内存读写 | **GDB适配层** | `SWD_Transfer()` 封装 | 构造 AP 读写请求 |
| 寄存器访问 | **GDB适配层** | `SWD_Transfer()` 封装 | 访问 DCRSR/DCRDR |
| 运行控制 | **GDB适配层** | `SWD_Transfer()` 封装 | 控制 DHCSR |
| 断点/观察点 | **GDB适配层** | `SWD_Transfer()` 封装 | 配置 FPB/DWT |
| 复位 | **GDB适配层** | `SWD_Transfer()` 或 GPIO | 软件/硬件复位 |

---

## 2. GDB Server 接口需求

### 2.1 Target 结构体接口 (target.h)

Blackmagic GDB Server 通过 `target_s` 结构体与底层交互，需要实现以下函数指针：

```c
typedef struct target {
    /* ===== Flash 操作 ===== */
    bool (*flash_erase)(struct target *, target_addr_t addr, size_t len);
    bool (*flash_write)(struct target *, target_addr_t dest, const void *src, size_t len);
    bool (*flash_complete)(struct target *);
    bool (*flash_mass_erase)(struct target *);
    
    /* ===== 内存访问 ===== */
    bool (*mem_read)(struct target *, void *dest, target_addr_t src, size_t len);
    bool (*mem_write)(struct target *, target_addr_t dest, const void *src, size_t len);
    bool (*mem_access_needs_halt)(struct target *);
    
    /* ===== 寄存器访问 ===== */
    size_t (*regs_size)(struct target *);
    void (*regs_read)(struct target *, void *data);
    void (*regs_write)(struct target *, const void *data);
    size_t (*reg_read)(struct target *, uint32_t reg, void *data, size_t max);
    size_t (*reg_write)(struct target *, uint32_t reg, const void *data, size_t size);
    const char *(*regs_description)(struct target *);
    
    /* ===== 运行控制 ===== */
    void (*reset)(struct target *);
    void (*halt_request)(struct target *);
    target_halt_reason_e (*halt_poll)(struct target *, target_addr64_t *watch);
    void (*halt_resume)(struct target *, bool step);
    void (*set_cmdline)(struct target *, const char *cmdline, size_t len);
    
    /* ===== 断点/观察点 ===== */
    int (*breakwatch_set)(struct target *, target_breakwatch_e type, 
                          target_addr_t addr, size_t len);
    int (*breakwatch_clear)(struct target *, target_breakwatch_e type, 
                            target_addr_t addr, size_t len);
    
    /* ===== 辅助功能 ===== */
    bool (*mem_map)(struct target *, char *buf, size_t len);
    int (*command)(struct target *, int argc, const char *argv[]);
    
    /* 私有数据 */
    void *priv;
} target_s;
```

### 2.2 接口实现映射表

| GDB 命令 | Blackmagic 接口 | 实现方式 | 调用函数 |
|----------|----------------|----------|----------|
| `vFlashErase` | `target_flash_erase()` | `TargetFlash::flash_erase_sector()` | Program |
| `vFlashWrite` | `target_flash_write()` | `TargetFlash::flash_program_page()` | Program |
| `vFlashDone` | `target_flash_complete()` | `TargetFlash::flash_uninit()` | Program |
| `m` (读内存) | `target_mem_read()` | AP 读: `SWD_Transfer(AP_TAR, addr)`, `SWD_Transfer(AP_DRW, &data)` | debug_probe/DAP |
| `M` (写内存) | `target_mem_write()` | AP 写: `SWD_Transfer(AP_TAR, addr)`, `SWD_Transfer(AP_DRW, data)` | debug_probe/DAP |
| `g` (读寄存器) | `target_regs_read()` | DCRSR: `SWD_Transfer(DBG_DCRSR, reg_num)`, `SWD_Transfer(DBG_DCRDR, &data)` | debug_probe/DAP |
| `G` (写寄存器) | `target_regs_write()` | 同上，写操作 | debug_probe/DAP |
| `c` (继续) | `target_halt_resume()` | DHCSR: `SWD_Transfer(DBG_DHCSR, C_DEBUGEN)` | debug_probe/DAP |
| `s` (单步) | `target_halt_resume(step=1)` | DHCSR: `SWD_Transfer(DBG_DHCSR, C_DEBUGEN\|C_STEP)` | debug_probe/DAP |
| `?` (暂停原因) | `target_halt_poll()` | DHCSR: `SWD_Transfer(DBG_DHCSR\|RnW, &data)` | debug_probe/DAP |
| `Z/z` (断点) | `target_breakwatch_set/clear()` | FPB: `SWD_Transfer(FPB_COMP0+i, addr\|1)` | debug_probe/DAP |
| `k` (复位) | `target_reset()` | AIRCR: `SWD_Transfer(AIRCR, SYSRESETREQ)` | debug_probe/DAP |

---

## 3. 详细接口实现方案

### 3.1 Flash 操作接口

**头文件**: `components/Program/inc/target_flash.h`

```cpp
// 使用 TargetFlash 类实现
class TargetFlash : public FlashIface {
public:
    err_t flash_init(const target_cfg_t &cfg);
    err_t flash_erase_sector(uint32_t addr);
    err_t flash_program_page(uint32_t addr, const uint8_t *buf, uint32_t size);
    err_t flash_uninit(void);
};
```

**适配层实现**:
```c
// gdb_target_adapter.c
#include "target_flash.h"

static TargetFlash s_flash;
static bool s_flash_inited = false;

static bool gdb_target_flash_erase(target_s *target, target_addr_t addr, size_t len) {
    if (!s_flash_inited) {
        // 从目标配置初始化 Flash
        target_cfg_t cfg = get_target_config(target);
        s_flash.swd_init(get_swd_interface());
        s_flash.flash_init(cfg);
        s_flash_inited = true;
    }
    
    // 按扇区擦除
    while (len > 0) {
        if (s_flash.flash_erase_sector(addr) != FlashIface::ERR_NONE) {
            return false;
        }
        uint32_t sector_size = s_flash.flash_erase_sector_size(addr);
        addr += sector_size;
        len -= sector_size;
    }
    return true;
}

static bool gdb_target_flash_write(target_s *target, target_addr_t dest, 
                                    const void *src, size_t len) {
    return s_flash.flash_program_page(dest, (const uint8_t *)src, len) 
           == FlashIface::ERR_NONE;
}

static bool gdb_target_flash_complete(target_s *target) {
    bool result = s_flash.flash_uninit() == FlashIface::ERR_NONE;
    s_flash_inited = false;
    return result;
}
```

### 3.2 内存访问接口 (使用 SWD_Transfer)

```c
// 直接使用 debug_probe 提供的 SWD_Transfer 函数
#include "DAP.h"

// AP 寄存器地址
#define AP_TAR      0x04    // Transfer Address Register
#define AP_DRW      0x0C    // Data Read/Write
#define AP_CSW      0x00    // Control and Status Word

// DP 寄存器地址
#define DP_RDBUFF   0x0C    // Read Buffer

// 构造 SWD 请求
#define SWD_REQ_APnDP       (1 << 0)
#define SWD_REQ_RnW         (1 << 1)
#define SWD_REQ_A32(a)      ((a) & 0x0C)

static bool dap_read_ap(uint32_t addr, uint32_t *data) {
    uint32_t req;
    uint8_t ack;
    
    // 1. 写 AP TAR (设置地址)
    req = SWD_REQ_APnDP | SWD_REQ_A32(AP_TAR);
    ack = SWD_Transfer(req, (uint32_t *)&addr);
    if (ack != DAP_TRANSFER_OK) return false;
    
    // 2. 读 AP DRW
    req = SWD_REQ_APnDP | SWD_REQ_RnW | SWD_REQ_A32(AP_DRW);
    ack = SWD_Transfer(req, data);
    if (ack != DAP_TRANSFER_OK) return false;
    
    // 3. 读 DP RDBUFF 获取最终数据
    req = SWD_REQ_RnW | SWD_REQ_A32(DP_RDBUFF);
    ack = SWD_Transfer(req, data);
    
    return ack == DAP_TRANSFER_OK;
}

static bool dap_write_ap(uint32_t addr, uint32_t data) {
    uint32_t req;
    uint8_t ack;
    
    // 1. 写 AP TAR (设置地址)
    req = SWD_REQ_APnDP | SWD_REQ_A32(AP_TAR);
    ack = SWD_Transfer(req, (uint32_t *)&addr);
    if (ack != DAP_TRANSFER_OK) return false;
    
    // 2. 写 AP DRW
    req = SWD_REQ_APnDP | SWD_REQ_A32(AP_DRW);
    ack = SWD_Transfer(req, &data);
    
    return ack == DAP_TRANSFER_OK;
}

static bool gdb_target_mem_read(target_s *target, void *dest, 
                                 target_addr_t src, size_t len) {
    uint8_t *dst = (uint8_t *)dest;
    
    // 设置 AP CSW: 32-bit, auto-increment
    dap_write_ap(AP_CSW, 0x23000050);
    
    while (len > 0) {
        uint32_t data;
        if (!dap_read_ap(src, &data)) {
            return false;
        }
        
        // 处理非对齐访问
        size_t offset = src & 3;
        size_t copy = 4 - offset;
        if (copy > len) copy = len;
        memcpy(dst, ((uint8_t *)&data) + offset, copy);
        
        src += copy;
        dst += copy;
        len -= copy;
    }
    return true;
}

static bool gdb_target_mem_write(target_s *target, target_addr_t dest, 
                                  const void *src, size_t len) {
    // 类似读操作，使用 dap_write_ap
    // ...
}
```

### 3.3 寄存器访问接口

Cortex-M 核心寄存器通过 CoreSight DCRSR/DCRDR 访问：

```c
// 寄存器编号 (GDB 顺序)
enum cortexm_regnum {
    REG_R0, REG_R1, REG_R2, REG_R3,
    REG_R4, REG_R5, REG_R6, REG_R7,
    REG_R8, REG_R9, REG_R10, REG_R11,
    REG_R12, REG_SP, REG_LR, REG_PC,
    REG_XPSR, REG_MSP, REG_PSP, 
    REG_PRIMASK, REG_CONTROL
};

// DCRSR (Debug Core Register Selector)
#define DCRSR_REG_WnR       (1 << 16)
#define DCRSR_REG_SEL_MASK  0x7F

static bool gdb_target_regs_read(target_s *target, void *data) {
    uint32_t *regs = (uint32_t *)data;
    
    // 通过 DAP 读取 R0-R15, xPSR
    for (int i = 0; i <= 16; i++) {
        // 写 DCRSR 选择寄存器
        DAP_WriteAP(DBG_DCRSR, i);
        // 读 DCRDR 获取值
        DAP_ReadAP(DBG_DCRDR, &regs[i]);
    }
    
    // MSP, PSP, PRIMASK, CONTROL 需要特殊处理
    // ...
    
    return true;
}
```

### 3.4 运行控制接口

```c
// DHCSR (Debug Halting Control and Status)
#define DHCSR_DBGKEY        0xA05F0000
#define DHCSR_C_DEBUGEN     (1 << 0)
#define DHCSR_C_HALT        (1 << 1)
#define DHCSR_C_STEP        (1 << 2)
#define DHCSR_S_HALT        (1 << 17)
#define DHCSR_S_RESET_ST    (1 << 25)

static void gdb_target_halt_request(target_s *target) {
    uint32_t dhcsr = DHCSR_DBGKEY | DHCSR_C_DEBUGEN | DHCSR_C_HALT;
    DAP_WriteAP(DBG_DHCSR, dhcsr);
}

static void gdb_target_halt_resume(target_s *target, bool step) {
    uint32_t dhcsr = DHCSR_DBGKEY | DHCSR_C_DEBUGEN;
    if (step) {
        dhcsr |= DHCSR_C_STEP;
    }
    DAP_WriteAP(DBG_DHCSR, dhcsr);
}

static target_halt_reason_e gdb_target_halt_poll(target_s *target, 
                                                  target_addr64_t *watch) {
    uint32_t dhcsr;
    DAP_ReadAP(DBG_DHCSR, &dhcsr);
    
    if (dhcsr & DHCSR_S_HALT) {
        // 检查暂停原因 (DHCSR.S_REGRDY, DFSR)
        // ...
        return TARGET_HALT_REQUEST;
    }
    return TARGET_HALT_RUNNING;
}
```

### 3.5 断点/观察点接口

```c
// FPB (Flash Patch and Breakpoint)
#define FPB_CTRL    0xE0002000
#define FPB_COMP(i) (0xE0002008 + 4 * (i))

static int gdb_target_breakwatch_set(target_s *target, 
                                      target_breakwatch_e type,
                                      target_addr_t addr, size_t len) {
    if (type == TARGET_BREAK_HARD) {
        // 硬件断点：使用 FPB
        for (int i = 0; i < max_hw_breakpoints; i++) {
            if (!breakpoint_used[i]) {
                // FPB_COMP = (addr & ~2) | 1 (enable)
                uint32_t comp = (addr & ~2) | 1;
                DAP_WriteAP(FPB_COMP(i), comp);
                breakpoint_used[i] = true;
                return 0;
            }
        }
    }
    // 软件断点/观察点...
    return -1;
}
```

---

## 4. 芯片配置管理

### 4.1 目标配置结构

```c
// target_config.h

typedef struct {
    const char *name;              // "STM32F103C8"
    uint32_t idcode;               // 0x1BA01477 (Cortex-M3)
    
    // Flash 配置
    struct {
        uint32_t start;            // 0x08000000
        uint32_t size;             // 0x00010000 (64KB)
        uint32_t sector_size;      // 0x400 (1KB)
        uint32_t write_size;       // 0x4 (4 bytes)
    } flash;
    
    // RAM 配置
    struct {
        uint32_t start;            // 0x20000000
        uint32_t size;             // 0x00005000 (20KB)
    } ram;
    
    // Flash 算法（Keil FLM 转换）
    const uint32_t *algo_blob;
    size_t algo_blob_size;
    uint32_t algo_stack_size;
    
    // 调试特性
    uint8_t has_fpb;               // 硬件断点支持
    uint8_t has_dwt;               // 观察点支持
} target_config_t;
```

### 4.2 配置来源

1. **内置配置**: 常用芯片（STM32 系列）内置到代码中
2. **配置文件**: JSON/YAML 格式的外部配置
3. **自动检测**: 通过 SWD 读取 IDCODE 和 ROM 表自动识别

---

## 5. 实现步骤

### 5.1 第一阶段：基础框架

1. 创建 `components/gdb_server/` 目录
2. 移植 Blackmagic 的 `gdb_packet.c/h`, `gdb_main.c/h`, `gdb_if.c/h`
3. 创建 `target_adapter.c/h` 实现 target_s 结构体
4. 实现基础连接/断开功能

### 5.2 第二阶段：调试功能

1. 通过 DAP 实现内存读写
2. 实现寄存器访问
3. 实现运行控制 (halt/resume/step)
4. 实现复位功能

### 5.3 第三阶段：Flash 烧录

1. 集成 TargetFlash 接口
2. 实现 vFlashErase/Write/Done
3. 支持 HEX 文件解析和烧录

### 5.4 第四阶段：高级功能

1. 硬件断点/观察点
2. 自动芯片检测
3. 多目标支持（可选）

---

## 6. 关键问题

### 6.1 并发访问

如果 USBIP 和 GDB Server 同时运行，需要互斥访问 SWD：

```c
static pthread_mutex_t swd_mutex = PTHREAD_MUTEX_INITIALIZER;

bool target_lock(void) {
    return pthread_mutex_lock(&swd_mutex) == 0;
}

void target_unlock(void) {
    pthread_mutex_unlock(&swd_mutex);
}
```

### 6.2 芯片复位策略

- **软件复位**: 写 AIRCR.SYSRESETREQ（推荐，不丢连接）
- **硬件复位**: 控制 NRST GPIO（需要额外引脚）
- **上电复位**: 控制目标电源（复杂）

### 6.3 Flash 算法来源

- 方案1: 编译时内置（C 数组）
- 方案2: 运行时从文件系统加载 `.FLM` 文件
- 方案3: 使用 OpenOCD 风格的算法描述

---

## 7. 文件结构规划

```
components/gdb_server/
├── CMakeLists.txt
├── gdb_if.c/h              # TCP 接口（port 2000）
├── gdb_packet.c/h          # GDB RSP 包处理
├── gdb_main.c/h            # GDB 主循环和命令处理
├── target_adapter.c/h      # target_s 适配层实现
├── target_config.c/h       # 芯片配置管理
├── target_flash.c/h        # Flash 操作适配（调用 Program）
├── target_debug.c/h        # 调试操作适配（调用 DAP）
├── hex_utils.c/h           # 十六进制转换工具
└── buffer_utils.c/h        # 缓冲区操作工具
```

---

## 8. 接口汇总表

| 功能类别 | 接口数量 | 实现复杂度 | 依赖组件 |
|----------|----------|------------|----------|
| Flash 操作 | 4 | 中 | Program/TargetFlash |
| 内存访问 | 2 | 低 | usbipd/DAP |
| 寄存器访问 | 5 | 中 | usbipd/DAP |
| 运行控制 | 5 | 低 | usbipd/DAP |
| 断点/观察点 | 2 | 高 | usbipd/DAP/FPB |
| 辅助功能 | 3 | 低 | - |
| **总计** | **21** | - | - |
