# USBIP Server

[English](README.md) | [中文](docs/README_zh.md)

A USBIP server implementation with three-layer architecture, running on Raspberry Pi 5, supporting CMSIS-DAP virtual debug devices.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                      main.c                              │
│                   (Server Main Loop)                    │
└──────────────────────┬──────────────────────────────────┘
                       │
         ┌─────────────┼─────────────┐
         │             │             │
         ▼             ▼             ▼
┌─────────────┐ ┌─────────────┐ ┌─────────────┐
│ transport.h │ │usbip_proto.h│ │device_drv.h │
│  Transport  │ │   Protocol  │ │    Driver   │
└─────────────┘ └─────────────┘ └─────────────┘
         │             │             │
         ▼             ▼             ▼
┌─────────────┐ ┌─────────────┐ ┌─────────────┐
│transport_tcp│ │usbip_proto.c│ │hid_dap/bulk_dap│
│    .c       │ │             │ │    .c       │
└─────────────┘ └─────────────┘ └─────────────┘
```

1. **Transport Layer** - Abstract network transport interface (TCP/Serial/Custom)
2. **Protocol Layer** - USBIP protocol encoding/decoding, decoupled from transport
3. **Device Driver Layer** - Device driver abstract interface for custom USB devices

---

## Virtual Devices

### 1. CMSIS-DAP v1 HID Debugger (Bus ID: 2-1)
- **VID:PID**: c251:4001
- **Interface**: HID (03:00:00)
- **Endpoint**: EP1 IN/OUT (Interrupt)
- **Features**: SWD debug protocol, supports OpenOCD/PyOCD

### 2. CMSIS-DAP v2 Bulk Debugger (Bus ID: 2-2) - Primary Use
- **VID:PID**: c251:4002
- **Interface**: Vendor Specific (FF:00:00)
- **Endpoint**: EP1 IN/OUT (Bulk, 64 bytes)
- **Features**: SWD debug protocol, high-speed transfer

---

## Directory Structure

```
usbip-server/
├── include/
│   ├── hal/                      # HAL layer headers
│   │   ├── usbip_log.h           # Log system
│   │   ├── usbip_osal.h          # OSAL interface
│   │   └── usbip_transport.h     # Transport interface
│   ├── usbip_protocol.h          # Protocol definitions
│   ├── usbip_devmgr.h            # Device driver interface
│   └── usbip_server.h            # Server main interface
├── src/
│   ├── hal/                       # HAL implementation
│   │   ├── usbip_log.c           # Log system
│   │   ├── usbip_osal.c          # OSAL core
│   │   ├── usbip_transport.c     # Transport core
│   │   └── usbip_mempool.c       # Memory pool
│   ├── server/                    # Server core
│   │   ├── usbip_protocol.c      # Protocol encoding
│   │   ├── usbip_server.c        # Connection management
│   │   ├── usbip_urb.c           # URB processing
│   │   ├── usbip_devmgr.c        # Device management
│   │   └── usbip_control.c       # Control transfer framework
│   ├── device/                    # Device driver base classes
│   │   ├── usbip_hid.c           # HID base class
│   │   └── usbip_bulk.c          # Bulk base class
│   ├── hid_dap.c                 # HID DAP v1 driver
│   ├── bulk_dap.c                # Bulk DAP v2 driver
│   ├── transport_tcp.c            # TCP transport implementation
│   ├── osal_posix.c               # POSIX OSAL implementation
│   └── main.c                     # Main program
├── components/
│   └── debug_probe/               # CMSIS-DAP implementation
│       ├── debug_gpio.c           # GPIO bit-banging
│       ├── debug_gpio.h           # GPIO definitions
│       ├── swd.c                  # SWD protocol
│       └── DAP/                   # CMSIS-DAP core
├── Kconfig                        # Kconfig configuration
├── scripts/
│   └── gen_config.py              # Config generation script
└── README.md
```

---

## Build

```bash
# Configure and build
cmake -B build -S .
cmake --build build

# Debug mode
cmake -B build -S . -DDEBUG=ON
cmake --build build
```

---

## Configuration

Uses Linux kernel-like Kconfig configuration system.

### Config Files

- `Kconfig` - Configuration options definition
- `.config` - User configuration (generated by gen_config.py)

### Configuration Options

| Option | Description | Default |
|--------|-------------|---------|
| `USBIP_SERVER_PORT` | Server listen port | 3240 |
| `LOG_LEVEL` | Global log level | 3 (INF) |
| `MAIN_LOG_LEVEL` | Main module log level | 3 (INF) |
| `SERVER_LOG_LEVEL` | Server module log level | 3 (INF) |
| `URB_LOG_LEVEL` | URB module log level | 3 (INF) |
| `DEVMGR_LOG_LEVEL` | Device manager log level | 3 (INF) |
| `CONTROL_LOG_LEVEL` | Control transfer log level | 4 (DBG) |
| `OSAL_LOG_LEVEL` | OSAL module log level | 3 (INF) |
| `TRANSPORT_LOG_LEVEL` | Transport module log level | 3 (INF) |
| `DAP_LOG_LEVEL` | HID DAP module log level | 3 (INF) |
| `BULK_DAP_LOG_LEVEL` | Bulk DAP module log level | 3 (INF) |
| `HID_LOG_LEVEL` | HID device log level | 3 (INF) |
| `BULK_LOG_LEVEL` | Bulk device log level | 3 (INF) |

### GPIO Configuration

| Option | Description | Default |
|--------|-------------|---------|
| `GPIO_SWCLK` | SWD clock pin | 17 |
| `GPIO_SWDIO` | SWD data pin | 27 |
| `GPIO_TCK` | JTAG clock pin | 17 |
| `GPIO_TMS` | JTAG mode select pin | 27 |
| `GPIO_TDI` | JTAG data input pin | 22 |
| `GPIO_TDO` | JTAG data output pin | 23 |

### Log Levels

| Value | Level | Description |
|-------|-------|-------------|
| 0 | LOG_LEVEL_NONE | No output |
| 1 | LOG_LEVEL_ERR | Errors only |
| 2 | LOG_LEVEL_WRN | Warnings |
| 3 | LOG_LEVEL_INF | Info (default) |
| 4 | LOG_LEVEL_DBG | Debug |

### Configuration Steps

```bash
# 1. Edit .config file
vim .config

# Example: change port and log level
CONFIG_USBIP_SERVER_PORT=3241
CONFIG_USBIP_LOG_LEVEL=4

# 2. Regenerate config
python scripts/gen_config.py

# 3. Rebuild
cmake -B build -S .
cmake --build build
```

### Reset to Defaults

```bash
rm .config
python scripts/gen_config.py
cmake -B build -S .
cmake --build build
```

---

## Run

```bash
# Load kernel modules
sudo modprobe usbip-core
sudo modprobe usbip-host
sudo modprobe vhci-hcd

# Pre-configure GPIO output mode
sudo pinctrl set 17 op dh
sudo pinctrl set 27 op dh

# Start server (default port 3240)
sudo build/usbip-server

# Specify port
sudo build/usbip-server -p 3240

# Show help
./build/usbip-server -h
```

---

## Debugger Testing

```bash
# Attach HID DAP v1 device
sudo usbip attach -r localhost -b 2-1

# Attach Bulk DAP v2 device
sudo usbip attach -r localhost -b 2-2
```

### OpenOCD Flash (Recommended)

```bash
# Build OpenOCD (requires Raspberry Pi toolchain)
./test_flash.sh test/LED.hex openocd bulk

# Or use HID device
./test_flash.sh test/LED.hex openocd hid
```

### PyOCD Flash

```bash
./test_flash.sh test/LED.hex pyocd bulk
./test_flash.sh test/LED.hex pyocd hid
```

---

## Protocol Support

- [x] OP_REQ_DEVLIST / OP_REP_DEVLIST
- [x] OP_REQ_IMPORT / OP_REP_IMPORT
- [x] USBIP_CMD_SUBMIT / USBIP_RET_SUBMIT
- [x] USBIP_CMD_UNLINK / USBIP_RET_UNLINK
- [x] Control Transfer (EP0)
- [x] Interrupt Transfer
- [x] Bulk Transfer

---

## License

Apache-2.0