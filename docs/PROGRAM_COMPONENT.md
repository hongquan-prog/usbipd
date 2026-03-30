# Program 组件 - 功能框架

## 模块定位

Program 组件提供 **脱机烧录（Offline Programming）** 功能，负责将固件文件（BIN/HEX）烧录到目标设备的 Flash 中。

## 功能架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         Program 组件                             │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │  文件解析层  │  │  编程控制层  │  │      Flash 操作层        │  │
│  │  HEX Parser │─►│ FileProgrammer│─►│    TargetFlash          │  │
│  └─────────────┘  └──────┬──────┘  └──────────┬──────────────┘  │
│                          │                      │                 │
│                          ▼                      ▼                 │
│                   ┌─────────────┐       ┌─────────────┐          │
│                   │ BinProgram  │       │   SWDIface  │          │
│                   │ HexProgram  │       │  TargetSWD  │          │
│                   └─────────────┘       └──────┬──────┘          │
│                                                │                  │
└────────────────────────────────────────────────┼──────────────────┘
                                                 │
                                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                      debug_probe (外部组件)                       │
└─────────────────────────────────────────────────────────────────┘
```

## 模块划分

| 层级 | 模块 | 功能 | 核心文件 |
|------|------|------|----------|
| **文件解析** | HEX Parser | 解析 Intel HEX 格式 | `hex_parser.c/h` |
| **编程接口** | ProgramIface | 编程器抽象接口 | `program_iface.h` |
| | BinProgram | BIN 文件烧录 | `bin_program.cpp/h` |
| | HexProgram | HEX 文件烧录 | `hex_program.cpp/h` |
| | FileProgrammer | 文件烧录入口 | `file_programmer.cpp/h` |
| **Flash 操作** | FlashIface | Flash 操作抽象接口 | `flash_iface.h` |
| | TargetFlash | Flash 算法执行器 | `target_flash.cpp/h` |
| **调试接口** | SWDIface | SWD 调试抽象接口 | `swd_iface.h` |
| | TargetSWD | SWD 接口实现 | `target_swd.cpp/h` |
| | SWD Host | 底层 SWD 操作 | `swd_host.c/h` |

## 数据流

```
固件文件 → FileProgrammer → (BinProgram/HexProgram) → TargetFlash → SWD → debug_probe
```

## 当前状态

| 功能 | 状态 | 备注 |
|------|------|------|
| HEX 解析 | ✅ 可用 | C 实现，无依赖 |
| SWD Host | ✅ 可用 | C 实现，无依赖 |
| Flash 算法执行 | ⚠️ 需适配 | 依赖 FreeRTOS 延时 |
