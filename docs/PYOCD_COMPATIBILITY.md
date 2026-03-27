# PyOCD/OpenOCD/Keil 兼容性说明

## 当前状态

✅ **OpenOCD**: 完全支持（HID 和 Bulk 模式）  
✅ **PyOCD HID**: 完全支持，需要应用补丁  
✅ **PyOCD Bulk**: 完全支持，需要应用补丁  
✅ **Keil MDK HID**: 完全支持  
✅ **Keil MDK Bulk**: 完全支持  

## 测试记录

### HID 模式 (2-1)
- OpenOCD: 5/5 次烧录成功
- PyOCD: 5/5 次烧录成功（应用补丁后）

### Bulk 模式 (2-2)
- OpenOCD: 5/5 次烧录成功
- PyOCD: 5/5 次烧录成功（应用补丁后）

## PyOCD 必需的补丁

### 补丁 1: pyusb_backend.py (HID 模式)

文件：`/home/lhq/.local/lib/python3.13/site-packages/pyocd/probe/pydapaccess/interface/pyusb_backend.py`

#### 1.1 修复卡死问题

```python
def start_rx(self):
    # 将 while True 改为有限次数
    max_attempts = 10
    try:
        for _ in range(max_attempts):
            self.ep_in.read(self.ep_in.wMaxPacketSize, 10)
    except usb.core.USBError:
        pass
```

#### 1.2 修复烧录不稳定

将 `read_sem.release()` 移到数据写入之后：

```python
def write(self, data):
    report_size = self.packet_size
    if self.ep_out:
        report_size = self.ep_out.wMaxPacketSize

    data.extend([0] * (report_size - len(data)))

    # 先写入数据
    if not self.ep_out:
        # 控制传输
        bmRequestType = 0x21
        bmRequest = 0x09
        wValue = 0x200
        wIndex = self.intf_number
        self.dev.ctrl_transfer(bmRequestType, bmRequest, wValue, wIndex, data,
                timeout=self.DEFAULT_USB_TIMEOUT_MS)
    else:
        # 中断传输
        self.ep_out.write(data, timeout=self.DEFAULT_USB_TIMEOUT_MS)

    # 后释放信号量（关键修复）
    self.read_sem.release()
```

### 补丁 2: pyusb_v2_backend.py (Bulk 模式)

文件：`/home/lhq/.local/lib/python3.13/site-packages/pyocd/probe/pydapaccess/interface/pyusb_v2_backend.py`

```python
def write(self, data):
    """@brief Write data on the OUT endpoint."""
    if TRACE.isEnabledFor(logging.DEBUG):
        TRACE.debug("  USB OUT> (%d) %s", len(data), ' '.join([f'{i:02x}' for i in data]))

    # 先写入数据
    self.ep_out.write(data, timeout=self.DEFAULT_USB_TIMEOUT_MS)

    # 发送 ZLP (Zero Length Packet)
    if (len(data) > 0) and (len(data) < self.packet_size) and (len(data) % self.ep_out.wMaxPacketSize == 0):
        self.ep_out.write(b'', timeout=self.DEFAULT_USB_TIMEOUT_MS)

    # 后释放信号量（关键修复）
    self.read_sem.release()
```

## 使用方法

### 1. 启动服务器

```bash
cd ~/Workspace/usbip-server
sudo ./build/usbip-server
```

### 2. 连接虚拟设备

HID 模式：
```bash
sudo usbip attach -r 127.0.0.1 -b 2-1
```

Bulk 模式：
```bash
sudo usbip attach -r 127.0.0.1 -b 2-2
```

### 3. 测试烧录

使用测试脚本：
```bash
# HID 模式
./test/test_flash.sh test/LED.hex openocd hid
./test/test_flash.sh test/LED.hex pyocd hid

# Bulk 模式
./test/test_flash.sh test/LED.hex openocd bulk
./test/test_flash.sh test/LED.hex pyocd bulk
```

手动测试：
```bash
# OpenOCD
openocd -f interface/cmsis-dap.cfg \
    -c "transport select swd" \
    -c "adapter speed 1000" \
    -c "init" \
    -c "program test/LED.hex verify reset" \
    -c "exit"

# PyOCD
pyocd load test/LED.hex -t stm32h750vbtx
```

## 问题根因分析

### PyOCD 信号量同步问题

PyOCD 的 `write()` 方法原实现：

```python
def write(self, data):
    self.read_sem.release()  # 先释放信号量
    self.ep_out.write(data)   # 后写入数据
```

这导致 `rx_task` 在数据准备好之前就开始读取，读到空数据导致 `bytearray index out of range` 错误。

修复方法：将 `read_sem.release()` 移到数据写入之后。

## 已知限制

1. **PyOCD 升级后需要重新应用补丁**
2. **HID 和 Bulk 模式需要分别打补丁**（两个不同的后端文件）

## 提交 PyOCD Issue

建议向 PyOCD 提交 Issue，修复信号量同步顺序问题：

**标题**: Fix race condition in USB backend write/read synchronization

**描述**: 
The `write()` method releases `read_sem` before writing data to the endpoint, causing `rx_task` to read empty data. This leads to `bytearray index out of range` errors when using virtual USB devices via usbip.

**Fix**: Move `self.read_sem.release()` after `self.ep_out.write()` in both `pyusb_backend.py` and `pyusb_v2_backend.py`.

**Files to modify**:
- `pyocd/probe/pydapaccess/interface/pyusb_backend.py`
- `pyocd/probe/pydapaccess/interface/pyusb_v2_backend.py`

## Keil MDK 兼容性

### 测试状态

✅ **Keil MDK 5.38+ HID 模式**: 完全支持，Flash Download 正常  
✅ **Keil MDK 5.38+ Bulk 模式**: 完全支持，Flash Download 正常  

### 关键修复

#### 修复 1: HID IN 端点返回实际长度

**问题**: Keil MDK 的 HID 驱动期望 IN 端点返回实际响应长度，而不是固定的 64 字节。返回 64 字节会导致 Keil 解析 DAP 响应时出错，表现为 "RDDI-DAP Error"。

**修复** (`src/device/virtual_dap.c`):

```c
// 修改前：返回固定 64 字节
*data_out = osal_malloc(DAP_REPORT_SIZE);
memset(*data_out, 0, DAP_REPORT_SIZE);
memcpy(*data_out, vdap.response, vdap.response_len);
*data_len = DAP_REPORT_SIZE;
urb_ret->u.ret_submit.actual_length = DAP_REPORT_SIZE;

// 修改后：返回实际长度
size_t actual_len = vdap.response_len;
*data_out = osal_malloc(actual_len);
memcpy(*data_out, vdap.response, actual_len);
*data_len = actual_len;
urb_ret->u.ret_submit.actual_length = actual_len;
```

#### 修复 2: DAP 状态共享

**问题**: HID 和 Bulk 两个设备都调用 `DAP_Setup()` 会互相重置全局 DAP 状态，导致连接丢失。

**修复**: 使用静态标志确保 `DAP_Setup()` 只调用一次。

```c
// virtual_dap.c
static int dap_initialized = 0;
if (!dap_initialized) {
    DAP_Setup();
    dap_initialized = 1;
}

// virtual_dap_v2.c
extern int dap_initialized;
if (!dap_initialized) {
    DAP_Setup();
    dap_initialized = 1;
}
```

### Keil 配置建议

1. **Debug 设置**:
   - Debug: CMSIS-DAP Debugger
   - Port: SW
   - Max Clock: 1-4 MHz（根据目标板调整）

2. **Flash Download 设置**:
   - 选择正确的 Programming Algorithm
   - 勾选 "Reset and Run"
   - 下载前可执行 "Erase Full Chip"

### 已知问题

- Keil 在连接时可能会发送不同长度的 HID 报告（2, 4, 5, 6, 8, 23, 45, 61, 63 字节等），已通过 `hid_normalize_report_id` 函数处理
- 某些版本的 Keil 可能需要降低 SWD 时钟到 1MHz 以下才能稳定工作
