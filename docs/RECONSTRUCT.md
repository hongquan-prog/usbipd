# 需求模板

> 参考Linux内核实现USBIP服务器，支持USBIP协议

## 📌 一句话总结

参考Linux内核和已经编写好的部分代码在树莓派5B中实现功能完备的USBIP服务器。


## 🎯 目标

### 要做什么？

在树莓派5B上实现USBIP服务器，并添加虚拟HID DAP和Bulk DAP调试器，使其能够在Windows、Linux等操作系统上使用USBIP协议进行调试和编程。便于后续移植到ESP32上。

### 为什么做？

- ESP32的USBIP服务器功能，使ESP32能够通过USBIP协议与Windows、Linux等操作系统进行调试和编程。
- 可以在无线的网络环境下进行调试和编程，无需物理连接USB线，这对于调试而言是非常方便的。


### 最终成果应该是什么？
- 在树莓派5B上成功实现USBIP服务器功能
- 能够在树莓派本机上可以通过USBIP客户端进行连接、OpenOCD和PyOCD可以正常工作
- 能够在Keil MDK中使用虚拟DAP调试器进行调试和编程
- USBIP符合USBIP协议规范

---

## 📂 现状

### 当前代码状态
- [x] 基本功能已实现
- [x] **OpenOCD**: HID 和 Bulk 模式均正常工作
- [x] **PyOCD**: HID 和 Bulk 模式正常工作（需要打补丁）
- [x] **Keil MDK**: HID 和 Bulk 模式均正常工作
- [ ] 需要检查代码的依赖关系，避免耦合

### 相关文件位置
- `docs/ARCHITECTURE.md` - 详细架构设计文档

#### 核心架构
- `include/usbip_protocol.h` - USBIP 协议定义
- `include/usbip_devmgr.h` - 设备驱动接口
- `include/transport.h` - 传输层接口
- `src/server/usbip_urb.c` - URB 处理框架
- `src/server/usbip_devmgr.c` - 设备管理器

#### 设备驱动
- `src/device/virtual_dap.c` - CMSIS-DAP v1 HID 设备
- `src/device/virtual_dap_v2.c` - CMSIS-DAP v2 Bulk 设备 (主要调试设备)
- `src/device/virtual_msc.c` - MSC 大容量存储设备
- `src/device/virtual_cdc.c` - CDC ACM 串口设备
- `src/device/virtual_hid.c` - HID 通用设备基类
- `src/device/virtual_bulk.c` - Bulk 通用设备基类

#### 调试探针
- `components/debug_probe/debug_gpio.c` - GPIO bit-banging SWD 实现
- `components/debug_probe/debug_gpio.h` - GPIO 引脚定义
- `components/debug_probe/swd.c` - SWD 协议实现
- `components/debug_probe/DAP/Source/DAP.c` - CMSIS-DAP 核心实现


### 参考实现/文档
../../src/linux-6.12.75/drivers/usb/usbip

---

## ⚠️ 约束

### 技术栈限制
使用C语言实现，不使用C++。


### 不能改动的地方
src/transport.c
components/debug_probe 目录下所有文件


### 性能/兼容性要求
动态内存不超过10KB


### 代码风格
- 遵循 CLAUDE.md 规范
- 遵循现有代码风格

## ✅ 验收标准

### 功能验证方式
本地测试可以参考CLAUDE.md中运行服务器和烧录测试的命令。


### 边界情况
<!-- 错误处理、异常情况、边界条件 -->


### 测试要求
- [ ] 新增单元测试
- [ ] 新增集成测试
- [ ] 现有测试必须全部通过
- [ ] 手动测试验证


---

## 📎 附加信息

### 相关 Issue/PR
<!-- 关联的 GitHub Issue、PR 编号 -->


### 优先级
- [ ] 🔴 高 - 编写符合USBIP协议规范的USBIP服务器，并添加HID和Bulk版本的DAP调试器
- [ ] 🟡 中 - 重新适配CDC和MSC设备并设计合适的测试用例进行自动化测试。
- [ ] 🟢 低 - 不符合代码规范的部分修正
