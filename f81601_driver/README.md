# Fintek F81601 PCIe CAN Driver

Modified Linux kernel driver for Fintek F81601 PCIe to CAN bus controllers, with extended support for device ID `0x2004`.

## Overview

This driver provides SocketCAN interface support for Fintek PCIe CAN bus cards:
- **Vendor ID:** `0x1c29` (Fintek)
- **Device IDs:** `0x1703` (standard), `0x2004` (extended)
- **Channels:** Up to 2 CAN channels per card
- **Controller:** SJA1000-based
- **Clock:** Internal 24MHz (default) or external clock

## Hardware Support

Tested with:
- Fintek F81601 PCIe CAN cards
- Device ID: `1c29:2004`
- Platform: NVIDIA Jetson (kernel 5.10.216-tegra)

## Building

### Prerequisites

```bash
# Install kernel headers if not already present
sudo apt-get install linux-headers-$(uname -r)
```

### Compile

```bash
make
```

This will create `f81601.ko` kernel module.

## Installation

### Temporary Installation (for testing)

```bash
# Load dependencies
sudo modprobe can-dev
sudo modprobe sja1000

# Load the driver
sudo insmod ./f81601.ko

# Verify CAN interfaces appeared
ip link show | grep can
```

### Permanent Installation

```bash
# Install to system modules directory
sudo make install

# The driver will auto-load when hardware is detected
```

## Usage

### Configure CAN Interface

```bash
# Set bitrate (example: 500 kbps)
sudo ip link set can0 type can bitrate 500000

# Bring interface up
sudo ip link set can0 up

# Check status
ip -details link show can0
```

### Monitor CAN Traffic

```bash
# Using candump
candump can0

# Using cansniffer
cansniffer can0
```

### Send CAN Messages

```bash
# Send a standard CAN frame
cansend can0 123#DEADBEEF
```

## Module Parameters

- `internal_clk` (bool, default: true)
  - Use internal 24MHz clock
  - Set to false to use external clock

- `external_clk` (uint, default: 0)
  - External clock frequency in Hz
  - Only used when `internal_clk=false`

### Example with External Clock

```bash
sudo insmod ./f81601.ko internal_clk=false external_clk=16000000
```

## Troubleshooting

### Check if hardware is detected

```bash
lspci -nn -d 1c29:
```

Should show:
```
0005:03:00.0 CANBUS [0c09]: Device [1c29:2004] (rev 01)
0005:04:00.0 CANBUS [0c09]: Device [1c29:2004] (rev 01)
```

### Check if driver is loaded

```bash
lsmod | grep f81601
```

### View driver messages

```bash
dmesg | grep -i can
dmesg | grep f81601
```

### Unload driver

```bash
sudo rmmod f81601
```

## Files

- `f81601.c` - Main driver source (modified to support device ID 0x2004)
- `sja1000.h` - SJA1000 CAN controller header
- `Makefile` - Build configuration
- `.gitignore` - Ignore build artifacts

## License

GPL v2 (same as Linux kernel)

## Credits

- Original driver: Peter Hong <peter_hong@fintek.com.tw>
- Modified for device ID 0x2004 support
- Based on Linux kernel v5.10
