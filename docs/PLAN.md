# USBIP 服务器重构规划

## 项目概述

在树莓派5B上实现功能完备的USBIP服务器，支持虚拟HID DAP和Bulk DAP调试器，能够在Windows、Linux等操作系统上使用USBIP协议进行调试和编程。

## 现状分析

### 已知问题

#### ✅ 已解决
1. ~~**PyOCD 兼容性问题**~~ - **已修复** (2026-03-24)
   - 根因：PyOCD 的 `write()` 方法中信号量释放顺序错误
   - 修复：将 `read_sem.release()` 移到数据写入之后
   - 涉及文件：`pyusb_backend.py` (HID) 和 `pyusb_v2_backend.py` (Bulk)

#### ✅ 已解决
2. ~~**Keil MDK 兼容性**~~ - **已修复** (2026-03-24)
   - 根因：HID IN 端点返回固定 64 字节而非实际长度
   - 修复：返回实际响应长度 `vdap.response_len`
   - 涉及文件：`src/device/virtual_dap.c`
3. **代码耦合**：需要检查代码依赖关系，避免耦合

### 已验证正常

| 工具 | HID 模式 | Bulk 模式 | 备注 |
|------|----------|-----------|------|
| OpenOCD | ✅ 正常 | ✅ 正常 | 无需修改 |
| PyOCD | ✅ 正常 | ✅ 正常 | 需要打补丁 |
| Keil MDK | ✅ 正常 | ✅ 正常 | 无需修改 |

**测试详情** (STM32H750, 6KB 固件):
- HID 模式: 12.5 KiB/s
- Bulk 模式: 12.5 KiB/s

## 目标

1. ~~**修复 PyOCD 兼容性问题**~~ ✅ **已完成**
2. ~~**测试 Keil MDK 兼容性**~~ ✅ **已完成**
3. **代码结构优化** - 降低耦合，提高模块化
4. **CDC/MSC 设备适配和测试**
5. **代码规范整理**

## 阶段规划

### 阶段 1: PyOCD 问题诊断与修复 ✅ **已完成** (2026-03-24)

#### 1.1 问题诊断
- [x] 分析 PyOCD 与 OpenOCD 的 USB 通信差异
- [x] 识别关键差异点（信号量同步时序）

#### 1.2 问题修复
- [x] 修复 `pyusb_backend.py` 信号量顺序
- [x] 修复 `pyusb_v2_backend.py` 信号量顺序
- [x] 添加服务器端 1ms 延迟优化
- [x] 验证 HID 和 Bulk 模式稳定性

**关键发现**:
PyOCD 的 `write()` 方法先释放 `read_sem` 再写入数据，导致 `rx_task` 读取空数据。
修复方法：将 `read_sem.release()` 移到 `ep_out.write()` 之后。

### 阶段 2: Keil MDK 兼容性 ✅ **已完成** (2026-03-24)

- [x] 在 Windows 环境下测试 Keil MDK 识别
- [x] 修复 Windows HID 驱动兼容性问题（返回实际长度而非 64 字节）
- [x] 验证调试和烧录功能（Flash Download 正常）

### 阶段 3: 代码结构优化（2-3天）

#### 3.1 解耦和模块化
- [ ] 分离设备驱动与协议层
- [ ] 优化设备管理器接口
- [ ] 统一错误处理机制

#### 3.2 内存优化
- [ ] 检查动态内存分配
- [ ] 优化静态内存使用
- [ ] 确保总内存 < 10KB

### 阶段 4: CDC/MSC 设备适配（2-3天）

- [ ] 重新适配 CDC ACM 串口设备
- [ ] 重新适配 MSC 大容量存储设备
- [ ] 编写自动化测试用例

### 阶段 5: 代码规范整理（1天）

- [ ] 统一代码风格（clang-format）
- [ ] 完善注释
- [ ] 更新架构文档

## 技术方案

### PyOCD 修复方案（已验证）

#### PyOCD 补丁
详见 `docs/PYOCD_COMPATIBILITY.md`

### 待解决问题

1. ~~**Keil MDK 兼容性**~~ ✅ **已完成** (2026-03-24)
   - 修复：HID IN 端点返回实际响应长度

2. **代码结构优化**
   - 设备驱动与协议层分离
   - 降低模块耦合度

## 调试记录

### PyOCD 修复过程

```bash
# 1. 发现问题：PyOCD 报错 "bytearray index out of range"
# 2. 根因分析：信号量同步顺序错误
# 3. 修复验证：
#    - pyusb_backend.py: 修改 write() 方法
#    - pyusb_v2_backend.py: 修改 write() 方法
#    - virtual_dap.c: 添加 osal_sleep_ms(1)
# 4. 稳定性测试：5/5 次烧录成功
```

### Keil MDK 修复过程

```bash
# 1. 发现问题：Keil 报错 "RDDI-DAP Error"，Flash Download 失败
# 2. 根因分析：HID IN 端点返回固定 64 字节，Keil 期望实际长度
# 3. 修复验证：
#    - virtual_dap.c: EP1 IN 返回 vdap.response_len 而非 DAP_REPORT_SIZE
# 4. 测试结果：Flash Download 正常，调试功能正常
```

## 验收标准

### 功能验证 ✅ 更新
- [x] PyOCD HID 模式可以正常烧录
- [x] PyOCD Bulk 模式可以正常烧录
- [x] Keil MDK 可以正常识别虚拟 DAP 设备
- [x] Keil MDK 可以正常调试和烧录
- [x] OpenOCD 保持正常工作

### 代码质量
- [ ] 所有现有测试通过
- [ ] 新增单元测试覆盖关键路径
- [ ] 代码风格符合 CLAUDE.md 规范
- [ ] 动态内存使用 < 10KB

### 文档
- [ ] 更新 ARCHITECTURE.md
- [x] 更新 PYOCD_COMPATIBILITY.md
- [x] 记录已知问题和解决方案

## 约束条件

1. **不可修改的文件**
   - `src/transport.c`
   - `components/debug_probe/` 目录下所有文件

2. **技术栈**
   - 使用 C 语言，不使用 C++
   - 遵循 CLAUDE.md 代码规范

3. **性能要求**
   - 动态内存不超过 10KB
   - 支持树莓派 5B 和 ESP32 移植

## 风险与应对

| 风险 | 影响 | 状态 | 应对措施 |
|------|------|------|----------|
| PyOCD 问题难以定位 | 高 | ✅ 已解决 | 分析信号量同步机制，定位根因 |
| ~~Windows 驱动问题~~ | 中 | ✅ 已解决 | HID IN 端点返回实际长度而非 64 字节 |
| 内存超限 | 中 | 监控中 | 持续监控内存使用，优化数据结构 |
| ~~Keil MDK 不兼容~~ | 中 | 已解决 | HID IN 端点返回实际长度而非 64 字节 |

## 下一步行动

1. ✅ **阶段 2 已完成** - Keil MDK 兼容性测试通过
   - HID 和 Bulk 模式均正常工作

2. **开始阶段 3** - 代码结构分析
   - 绘制模块依赖图
   - 识别高耦合模块

## 参考文档

- `docs/PYOCD_COMPATIBILITY.md` - PyOCD 兼容性详细说明
- `CLAUDE.md` - 代码规范
- `ARCHITECTURE.md` - 架构文档（待更新）
