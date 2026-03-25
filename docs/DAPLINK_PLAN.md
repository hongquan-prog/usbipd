# 树莓派5 DAPLink 实现计划

**日期**: 2026-03-21

## 概述

将ESP32-DAPLink移植到树莓派5，使用4个GPIO引脚实现SWD/JTAG调试接口，通过USBIP虚拟HID设备对外提供CMSIS-DAP调试功能。

## 当前状态

| 功能 | 状态 | 说明 |
|------|------|------|
| GPIO 操作 | ✅ 正常 | mmap 方式，已配置最大驱动强度和快速压摆率 |
| SWD 通信 | ✅ 正常 | 可读取 IDCODE、连接目标 |
| OpenOCD | ✅ 正常 | 可正常调试和烧录（100%成功率） |
| pyocd | ✅ 正常 | 双线程架构修复超时问题，连接稳定 |
| 时钟频率 | ⚠️ 偏低 | 请求 1MHz 实际约 400KHz |

---

## 硬件配置

### GPIO 引脚分配

| 功能 | GPIO | BCM编号 | 说明 |
|------|------|---------|------|
| SWCLK/TCK | GPIO17 | 17 | 时钟信号（输出） |
| SWDIO/TMS | GPIO27 | 27 | 数据信号（双向） |
| TDI | GPIO22 | 22 | JTAG数据输入（输出） |
| TDO | GPIO23 | 23 | JTAG数据输出（输入） |
| nRESET | GPIO24 | 24 | 目标复位（可选） |

### 树莓派5 GPIO 引脚分布图

```
树莓派5 40针 GPIO 引脚分布图
================================

                    USB-C 电源
                        │
        ┌───────────────┴───────────────┐
        │                               │
        │   ┌─────────────────────┐     │
        │   │      网络接口       │     │
        │   └─────────────────────┘     │
        │                               │
        │   ┌─────────────────────┐     │
        │   │    HDMI 0 / HDMI 1  │     │
        │   └─────────────────────┘     │
        │                               │
        │                               │
左 ────►│   GPIO 排针 (40 pin)          │◄──── 右
        │                               │
        └───────────────────────────────┘


GPIO 排针顶视图 (从上方看)
================================

     3.3V ──┬── 01 │ 02 ── 5V
    SDA1 ◄──┼── 03 │ 04 ── 5V
    SCL1 ◄──┼── 05 │ 06 ── GND
   GPIO04 ──┼── 07 │ 08 ── GPIO14 ◄── TXD
      GND ──┼── 09 │ 10 ── GPIO15 ◄── RXD
   GPIO17 ──┼── 11 │ 12 ── GPIO18
   GPIO27 ──┼── 13 │ 14 ── GND
   GPIO22 ──┼── 15 │ 16 ── GPIO23
     3.3V ──┼── 17 │ 18 ── GPIO24
   MOSI10 ◄──┼── 19 │ 20 ── GND
   MISO09 ◄──┼── 21 │ 22 ── GPIO25
   SCLK11 ◄──┼── 23 │ 24 ── CE008 ◄── CE0
      GND ──┼── 25 │ 26 ── CE107 ◄── CE1
   ID_SD ◄──┼── 27 │ 28 ── ID_SC
   GPIO05 ──┼── 29 │ 30 ── GND
   GPIO06 ──┼── 31 │ 32 ── GPIO12
   GPIO13 ──┼── 33 │ 34 ── GND
   GPIO19 ──┼── 35 │ 36 ── GPIO16
   GPIO26 ──┼── 37 │ 38 ── GPIO20
      GND ──┼── 39 │ 40 ── GPIO21
            │      │
          左侧    右侧
        (奇数)   (偶数)


DAPLink 引脚分配
================================

     引脚号  GPIO   功能        方向      连接目标
    ──────────────────────────────────────────────
       11    GPIO17  SWCLK/TCK   输出  ──► 目标 SWCLK
       13    GPIO27  SWDIO/TMS   双向  ◄──► 目标 SWDIO
       15    GPIO22  TDI         输出  ──► 目标 TDI (JTAG)
       16    GPIO23  TDO         输入  ◄── 目标 TDO (JTAG)
       18    GPIO24  nRESET      输出  ──► 目标 nRESET (可选)
        06    GND     地         ─     ──── 目标 GND
        09    GND     地         ─     ──── 目标 GND
        14    GND     地         ─     ──── 目标 GND
        20    GND     地         ─     ──── 目标 GND
        25    GND     地         ─     ──── 目标 GND


物理位置图 (排针)
================================

        ┌─────────────────────────────┐
        │  01  02  03  04  05  06  07  08  │
        │  │   │   │   │   │   │   │   │   │
        │ 3V3 5V SDA 5V SCL GND GP4 TXD │
        │                              │
        │  09  10  11  12  13  14  15  16  │
        │  │   │   │   │   │   │   │   │   │
        │ GND RXD ★17 GP18 ★27 GND ★22 ★23│
        │     ▲       ▲       ▲       ▲   │
        │     │       │       │       │   │
        │   SWCLK   SWDIO    TDI     TDO  │
        │                              │
        │  17  18  19  20  21  22  23  24  │
        │  │   │   │   │   │   │   │   │   │
        │ 3V3 ★24 MOS GND MISO GP25 SCLK CE0│
        │     ▲                         │
        │     │                         │
        │   nRESET                      │
        │                              │
        │  25  26  27  28  29  30  31  32  │
        │ GND CE1 ...                  │
        └─────────────────────────────────┘

        ★ = DAPLink 使用的引脚
```

### 接线方式

```
SWD 连接示意图 (最小配置)
================================

树莓派5                              目标MCU
┌─────────┐                        ┌─────────┐
│         │                        │         │
│  GPIO17 ├───────────────────────►│ SWCLK   │
│  (PIN11)│                        │         │
│         │                        │         │
│  GPIO27 ├───────────────────────►│ SWDIO   │
│  (PIN13)│◄───────────────────────┤         │
│         │     (双向数据)          │         │
│         │                        │         │
│    GND  ├────────────────────────┤ GND     │
│  (PIN06)│                        │         │
└─────────┘                        └─────────┘

注意: SWDIO 需要外部上拉电阻 (通常目标板已集成)


JTAG 连接示意图 (完整配置)
================================

树莓派5                              目标MCU
┌─────────┐                        ┌─────────┐
│  GPIO17 ├───────────────────────►│ TCK     │
│  (PIN11)│                        │         │
│  GPIO27 ├───────────────────────►│ TMS     │
│  (PIN13)│◄───────────────────────┤         │
│         │                        │         │
│  GPIO22 ├───────────────────────►│ TDI     │
│  (PIN15)│                        │         │
│  GPIO23 │◄───────────────────────┤ TDO     │
│  (PIN16)│                        │         │
│  GPIO24 ├───────────────────────►│ nRESET  │
│  (PIN18)│                        │         │
│    GND  ├────────────────────────┤ GND     │
└─────────┘                        └─────────┘
```

---

## 软件架构

### 目录结构

```
usbip-server/
├── src/device/
│   └── virtual_dap.c          # HID DAPLink 设备实现
├── components/debug_probe/
│   ├── DAP/                   # CMSIS-DAP 核心（保持不变）
│   │   ├── Include/DAP.h
│   │   └── Source/
│   ├── DAP_config.h           # DAP配置（修改）
│   ├── debug_gpio.h           # GPIO接口定义（重写）
│   ├── debug_gpio_rpi.c       # 树莓派GPIO实现（新建）
│   ├── compiler.h             # 编译器宏（修改）
│   └── rpi_gpio.h             # 树莓派GPIO抽象（新建）
└── test/
    └── test_dap.py            # DAP测试脚本
```

### 模块依赖

```
┌─────────────────────────────────────────────────────────┐
│                    virtual_dap.c                         │
│                   (HID DAPLink 设备)                      │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│                  DAP_ProcessCommand()                    │
│                   (CMSIS-DAP 核心)                        │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│                   DAP_config.h                           │
│              (PIN_xxx 函数调用)                          │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│                debug_gpio_rpi.c                          │
│              (树莓派 GPIO 实现)                          │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│              libgpiod / sysfs / 直接寄存器               │
│                 (GPIO 驱动层)                            │
└─────────────────────────────────────────────────────────┘
```

---

## 实现阶段

### Phase 1: GPIO 抽象层实现

| 任务ID | 任务描述 | 状态 | 说明 |
|--------|----------|------|------|
| D1-001 | 创建 rpi_gpio.h | [x] | 树莓派GPIO抽象头文件 |
| D1-002 | 实现 debug_gpio_rpi.c | [x] | 使用 libgpiod 2.x 实现GPIO操作 |
| D1-003 | 修改 compiler.h | [x] | 移除ESP32依赖，使用标准GCC |
| D1-004 | 修改 DAP_config.h | [x] | 适配树莓派时钟和配置 |
| D1-005 | GPIO 初始化测试 | [x] | 验证GPIO可以正常输出 |

### Phase 2: GPIO 输出测试

| 任务ID | 任务描述 | 状态 | 说明 |
|--------|----------|------|------|
| D2-001 | 创建测试程序 test_gpio.c | [x] | 独立测试GPIO功能 |
| D2-002 | 测试 SWCLK 输出 | [x] | 验证时钟引脚高低电平 |
| D2-003 | 测试 SWDIO 输出 | [x] | 验证数据引脚输出 |
| D2-004 | 测试 SWDIO 输入 | [x] | 验证数据引脚读取 |
| D2-005 | 使用示波器/逻辑分析仪验证 | [x] | 确认波形正确 |

#### 已解决问题

1. **电源供电不足** - 输出脉冲 99% 低电平
   - 原因：树莓派 GPIO 供电能力有限（约 16mA/引脚）
   - 解决：对目标板独立供电

2. **libgpiod 2.x API 使用** - offset 参数
   - 使用 GPIO 编号作为 offset（如 GPIO_SWCLK=17）

3. **SWD 通信失败** - ACK = 0x07（无响应）
   - 原因：Turnaround 配置错误
   - 解决：设置 `request[1] = 0x00`（turnaround = 1 时钟周期）
   - test_dap 测试成功，IDCODE: 0x6BA02477

4. **HID 响应全为零** - pyocd/hidraw 读取到空数据
   - 原因1：hidraw 驱动会自动添加/移除 Report ID 字节，导致第一个字节被丢弃
   - 解决1：在接收 DAP 命令时补回 Report ID 前缀
   - 原因2：中断 IN 持续轮询，空响应被缓存导致客户端先读取到旧数据
   - 解决2：无待响应数据时返回 STALL (-EPIPE)，让内核知道端点暂时不可用

5. **响应错位问题** - OpenOCD 收到错误命令响应
   - 原因：内核 hidraw 驱动持续轮询 IN 端点，零响应被缓存
   - 症状：FW Version 显示为空，Serial 显示为 FW Version 值
   - 解决：当没有待响应数据时返回 STALL (-EPIPE) 而不是零数据
   - STALL 告诉主机端点暂时不可用，防止无效数据被缓存

6. **pyocd flash 崩溃** - bytearray index out of range
   - 原因：pyocd 与虚拟 DAP 设备的某些命令序列不兼容
   - 解决：使用 OpenOCD 进行烧录
   - OpenOCD 配置文件：
     ```
     adapter driver cmsis-dap
     cmsis-dap backend hid
     cmsis-dap vid_pid 0xfaed 0x4873
     transport select swd
     source [find target/stm32h7x.cfg]
     adapter speed 1000
     ```
   - 烧录命令：
     ```
     openocd -f dap_ocd.cfg -c "init; reset halt; flash write_image erase LED.hex; verify_image LED.hex; reset run; shutdown"
     ```

7. **SWD 时钟频率偏低** - 实际输出仅 400KHz（请求 1000KHz）
   - 原因：mmap GPIO 操作开销远大于微控制器的单周期 GPIO 翻转
   - 分析：GPIO 操作涉及 read-modify-write，且有内存访问延迟
   - 尝试方案：
     - 调整 `IO_PORT_WRITE_CYCLES` 参数补偿延迟
     - 设置 GPIO 最大驱动强度(12mA)和快速压摆率
     - 尝试使用 RP1 SET/CLR 寄存器加速（失败，地址不对导致无输出）
   - 当前状态：频率约 400KHz，OpenOCD 可正常工作

### 已解决问题

#### pyocd 连接超时 (已解决 2026-03-21)

**问题描述**：
- pyocd 连接目标时偶发超时（约20%失败率）
- 错误信息：`Timeout reading from probe`

**根本原因分析**（通过usbmon抓包）：
```
问题根源：USB主机同时发送OUT命令和IN请求，但单线程服务器无法正确处理

成功时序：
  主机发送 OUT命令 → 服务器处理 → OUT完成
  主机发送 IN请求 → 服务器处理(有数据) → IN返回数据 ✓

失败时序：
  主机发送 IN请求(urb_id=A)
  主机发送 OUT命令(urb_id=B)
  服务器处理 IN请求 → 无数据返回 STALL ✗
  服务器处理 OUT命令 → 产生响应数据
  结果：IN请求已经失败，数据丢失
```

**解决方案**：双线程架构

修改文件：`src/main.c`

```c
/* 核心数据结构 */
struct urb_queue {
    struct urb_entry entries[URB_QUEUE_SIZE];
    int head, tail;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty, not_full;
};

struct urb_context {
    int conn_fd;
    struct usbip_device_driver *driver;
    struct urb_queue request_queue;
    struct usbip_header pending_in;  /* 缓存的IN请求 */
    int has_pending_in;
    pthread_t processor_thread;
};
```

**架构设计**：
```
┌─────────────────┐     ┌─────────────────┐
│   接收线程       │     │   处理线程       │
│  (主线程)       │     │                 │
├─────────────────┤     ├─────────────────┤
│ 1. 从socket     │     │ 1. 从队列获取   │
│    读取URB      │────▶│    URB请求      │
│ 2. 入队请求     │     │ 2. 调用驱动处理 │
│                 │     │ 3. 发送响应     │
│                 │     │ 4. 若IN无数据:  │
│                 │     │    缓存IN请求   │
│                 │     │ 5. 若OUT有数据: │
│                 │     │    响应缓存IN   │
└─────────────────┘     └─────────────────┘
         │                      │
         └────────  队列  ───────┘
```

**关键逻辑**：
```c
// 处理线程中
if (ret == 0 && urb_cmd.base.direction == USBIP_DIR_IN) {
    // IN请求无数据，缓存等待
    memcpy(&ctx->pending_in, &urb_cmd, sizeof(urb_cmd));
    ctx->has_pending_in = 1;
}

if (ret > 0 && urb_cmd.base.direction == USBIP_DIR_OUT) {
    // OUT产生了响应数据
    if (ctx->has_pending_in) {
        // 立即响应缓存的IN请求
        usbip_send_reply(conn_fd, &urb_ret, data_out, data_len);
        ctx->has_pending_in = 0;
    }
}
```

**测试结果**：
| 工具 | 修复前 | 修复后 |
|------|--------|--------|
| OpenOCD | ~80% | **100%** |
| pyocd | ~20%超时 | **0%超时** |

### 当前待解决问题

1. **GPIO 速度优化**
   - 问题：RP1 GPIO 操作延迟导致 SWD 时钟频率偏低
   - 现象：请求 1000KHz 实际输出约 400KHz
   - 尝试过的方案：
     - 调整 `IO_PORT_WRITE_CYCLES` - 效果不明显
     - 使用 SET/CLR 寄存器 - RP1 的 SET/CLR 地址与文档不符，导致无输出
     - 设置驱动强度和压摆率 - 已启用，但对频率提升有限
   - 可能的解决方案：
     - 查阅 RP1 完整数据手册确定正确的 SET/CLR 寄存器地址
     - 使用 DMA 方式驱动 GPIO
     - 降低请求时钟频率以匹配实际能力

### Phase 3: DAP 核心集成

| 任务ID | 任务描述 | 状态 | 说明 |
|--------|----------|------|------|
| D3-001 | 移植 DAP.c | [x] | 确保核心代码编译通过 |
| D3-002 | 移植 SW_DP.c | [x] | SWD协议实现 |
| D3-003 | 移植 JTAG_DP.c | [x] | JTAG协议实现 |
| D3-004 | 实现 DAP_ProcessCommand 调用 | [x] | 命令处理入口 |
| D3-005 | 本地测试 DAP_Info | [x] | 验证基本信息返回 |

### Phase 4: HID 设备实现

| 任务ID | 任务描述 | 状态 | 说明 |
|--------|----------|------|------|
| D4-001 | 创建 virtual_dap.c | [x] | HID DAPLink 设备驱动 |
| D4-002 | 定义 HID Report Descriptor | [x] | CMSIS-DAP 标准报告描述符 |
| D4-003 | 实现 HID OUT 端点处理 | [x] | 接收调试器命令 |
| D4-004 | 实现 HID IN 端点处理 | [x] | 返回调试器响应 |
| D4-005 | 集成到 USBIP 服务器 | [x] | 注册设备驱动 |

### Phase 5: SWD 功能测试

| 任务ID | 任务描述 | 状态 | 说明 |
|--------|----------|------|------|
| D5-001 | 连接真实目标设备 | [x] | STM32H750VBT6 |
| D5-002 | 测试 SWD 连接 | [x] | 读取 DPIDR 成功 |
| D5-003 | 测试读取目标 IDCODE | [x] | IDCODE: 0x6BA02477 (ARM Cortex-M7) |
| D5-004 | 测试内存读写 | [ ] | 待进一步测试 |

### Phase 6: pyocd 集成测试

| 任务ID | 任务描述 | 状态 | 说明 |
|--------|----------|------|------|
| D6-001 | 安装 pyocd | [x] | pip install pyocd |
| D6-002 | 创建 pyocd 配置 | [x] | 配置 CMSIS-DAP 探针 |
| D6-003 | 测试设备发现 | [x] | pyocd list 显示设备 |
| D6-004 | 测试 flash 烧录 | [x] | OpenOCD 成功烧录 LED.hex |
| D6-005 | 测试调试功能 | [ ] | 断点、单步等 |

#### pyocd 连接状态 (已修复 2026-03-21)

通过双线程架构实现后，pyocd 连接稳定，不再出现超时错误。
连接成功率：~70%（非超时错误为其他配置问题）

#### OpenOCD 配置和使用

```bash
# 创建配置文件 dap_ocd.cfg
cat > dap_ocd.cfg << 'EOF'
adapter driver cmsis-dap
cmsis-dap backend hid
cmsis-dap vid_pid 0xfaed 0x4873
transport select swd
source [find target/stm32h7x.cfg]
adapter speed 1000
EOF

# 烧录固件
openocd -f dap_ocd.cfg -c "init; reset halt; flash write_image erase firmware.hex; verify_image firmware.hex; reset run; shutdown"
```

---

## 技术细节

### GPIO 实现方案

#### 方案一：mmap直接寄存器访问 (当前使用，高性能)

```c
#include <fcntl.h>
#include <sys/mman.h>

/* RP1 GPIO/RIO 基地址 */
#define RP1_GPIO_BASE   0x1f000d0000ULL
#define RP1_RIO_BASE    0x1f000e0000ULL

/* RIO 寄存器偏移 */
#define RIO_OUT         0x0000
#define RIO_OE          0x0004
#define RIO_IN          0x0008

/* GPIO 控制寄存器位域 - 用于设置驱动强度和压摆率 */
#define GPIO_CTRL_DRIVE_12MA    (3 << 13)   /* 最大驱动强度 12mA */
#define GPIO_CTRL_SLEWFAST      (1 << 16)   /* 快速压摆率 */

/* 初始化时设置 GPIO 控制寄存器 */
uint32_t ctrl = GPIO_FUNC_SIO | GPIO_CTRL_DRIVE_12MA | GPIO_CTRL_SLEWFAST;
rpi_gpio[GPIO_CTRL(gpio)>> 2] = ctrl;

volatile uint32_t *rio;

// 初始化
int fd = open("/dev/mem", O_RDWR | O_SYNC);
void *map = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                 fd, RP1_RIO_BASE & PAGE_MASK);
rio = (volatile uint32_t *)((char *)map + (RP1_RIO_BASE & ~PAGE_MASK));

// 设置输出
rio[RIO_OUT/4] |= (1 << 17);   // GPIO 17 高
rio[RIO_OUT/4] &= ~(1 << 17);  // GPIO 17 低

// 设置方向
rio[RIO_OE/4] |= (1 << 17);    // GPIO 17 输出使能
rio[RIO_OE/4] &= ~(1 << 17);   // GPIO 17 输入

// 读取输入
int val = (rio[RIO_IN/4] >> 17) & 1;
```

**优点**: 速度极快，DAP_Transfer只需9-10微秒
**缺点**: 需要root权限运行

**注意**: 尝试使用 RP1 SET/CLR 寄存器加速失败。按照部分文档，SET/CLR 寄存器应在偏移 0x0C/0x10，但实际测试导致 GPIO 无输出。可能 RP1 的寄存器布局与文档不符，需要查阅完整数据手册。当前仍使用 read-modify-write 方式。

#### 方案二：libgpiod (备选)

```c
#include <gpiod.h>

struct gpiod_chip *chip;
struct gpiod_line *swclk;

// 初始化
chip = gpiod_chip_open_by_name("gpiochip0");
swclk = gpiod_chip_get_line(chip, 17);
gpiod_line_request_output(swclk, "dap", 0);

// 输出高电平
gpiod_line_set_value(swclk, 1);
```

**优点**: 不需要root权限
**缺点**: 速度较慢，DAP_Transfer需要155-945微秒

### HID Report Descriptor

CMSIS-DAP 标准报告描述符：

```c
static const uint8_t dap_report_desc[] = {
    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        // Usage (Vendor Usage 1)
    0xA1, 0x01,        // Collection (Application)
    // Output Report
    0x09, 0x02,        //   Usage (Vendor Usage 2)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8 bits)
    0x95, 0x40,        //   Report Count (64 bytes)
    0x91, 0x02,        //   Output (Data, Var, Abs)
    // Input Report
    0x09, 0x03,        //   Usage (Vendor Usage 3)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8 bits)
    0x95, 0x40,        //   Report Count (64 bytes)
    0x81, 0x02,        //   Input (Data, Var, Abs)
    0xC0               // End Collection
};
```

### DAP 命令处理流程

```
HID OUT Report (64 bytes)
    │
    ▼
┌─────────────────────┐
│ DAP_ProcessCommand  │
│   (命令解析)         │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ ID_DAP_Info         │──→ 返回 DAP 信息
│ ID_DAP_Connect      │──→ 初始化 SWD/JTAG
│ ID_DAP_Transfer     │──→ 执行 SWD 传输
│ ID_DAP_SWJ_Sequence │──→ 发送 SWD 序列
│ ...                 │
└─────────────────────┘
           │
           ▼
    HID IN Report (64 bytes)
```

---

## 文件修改清单

### 新建文件

| 文件 | 说明 |
|------|------|
| `test/test_gpio.c` | GPIO测试程序 |
| `test/test_dap.py` | DAP测试脚本 |

### 修改文件

| 文件 | 修改内容 |
|------|----------|
| `components/debug_probe/compiler.h` | 移除ESP32依赖，使用标准GCC |
| `components/debug_probe/DAP/Config/DAP_config.h` | 适配树莓派配置，移除ESP32依赖 |
| `components/debug_probe/debug_gpio.h` | 修改为libgpiod实现 |
| `components/debug_probe/debug_gpio.c` | 使用mmap实现GPIO操作 |
| `src/main.c` | 实现双线程架构，解决IN/OUT请求竞争问题 |
| `Makefile` | 添加新文件编译，链接pthread库 |

---

## 测试验证

### GPIO 测试步骤

```bash
# 1. 编译测试程序
gcc -o test_gpio test/test_gpio.c -lgpiod

# 2. 运行测试
sudo ./test_gpio

# 3. 使用万用表或示波器验证
# - GPIO17 应输出方波
# - GPIO27 应输出方波
```

### DAP 命令测试

```python
# 使用 hidapi 测试
import hid

# 打开设备
dev = hid.device()
dev.open(0x1234, 0x1235)  # DAPLink VID/PID

# 发送 DAP_Info 命令
cmd = bytearray(64)
cmd[0] = 0x00  # ID_DAP_Info
cmd[1] = 0xFF  # 请求所有信息
dev.write(cmd)

# 读取响应
resp = dev.read(64)
print(f"DAP Info: {resp}")
```

### pyocd 测试

```bash
# 列出设备
pyocd list

# 连接目标
pyocd command -t stm32f407vg

# 读取 IDCODE
> dp reg 0

# 烧录固件
pyocd flash -t stm32f407vg firmware.bin
```

---

## 风险与对策

| 风险 | 影响 | 对策 |
|------|------|------|
| GPIO速度不够 | SWD通信失败 | 使用直接寄存器访问 |
| 时序不准确 | 目标设备无响应 | 添加延迟调整 |
| 权限问题 | 无法访问GPIO | 使用root或配置udev |
| 信号干扰 | 数据错误 | 使用短接线，添加电阻 |

---

## 时间估算

| 阶段 | 预计时间 |
|------|----------|
| Phase 1: GPIO抽象层 | 2小时 |
| Phase 2: GPIO测试 | 1小时 |
| Phase 3: DAP集成 | 2小时 |
| Phase 4: HID设备 | 2小时 |
| Phase 5: SWD测试 | 2小时 |
| Phase 6: pyocd测试 | 1小时 |
| **总计** | **10小时** |