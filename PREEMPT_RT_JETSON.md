# PREEMPT_RT Kernel on Jetson AGX Orin

## System Info

| Item | Value |
|------|-------|
| Board | NVIDIA Jetson AGX Orin (leetop gitKit) |
| L4T | R36.4.4 |
| JetPack | 6.x |
| OS | Ubuntu 22.04.5 LTS (Jammy) |
| RT Kernel | `5.15.148-rt-tegra` |
| Architecture | aarch64 |

---

## Overview

NVIDIA's L4T R36.4.4 kernel source already includes the PREEMPT_RT patches in the
source tree (`debian.realtime/`, RT locking primitives in `kernel/locking/`). No
manual patch download is needed — `nvbuild.sh -r` handles everything.

---

## Build Steps

### 1. Download kernel sources

From [https://developer.nvidia.com/embedded/jetson-linux-r3644](https://developer.nvidia.com/embedded/jetson-linux-r3644),
download **Driver Package Sources** (`public_sources.tbz2`). Direct CDN link works with wget:

```bash
wget -c "https://developer.download.nvidia.com/embedded/L4T/r36_Release_v4.4/sources/public_sources.tbz2"
```

### 2. Extract sources

```bash
tar xf public_sources.tbz2
cd Linux_for_Tegra/source
tar xf kernel_src.tbz2
tar xf kernel_oot_modules_src.tbz2
```

### 3. Extract display driver source

The `nvdisplay` directory is in a separate archive and must be extracted into the
same `source/` directory. Use the **with-root-dir** variant:

```bash
tar xf nvidia_kernel_display_driver_source.tbz2
```

> **Note:** Do NOT extract `nvidia_kernel_display_driver_source_without_root_dir.tbz2`
> into the source root — it will overwrite the OOT `Makefile`.

### 4. Build dependencies

All required packages were already present on this system:

```bash
sudo apt install -y build-essential bc flex bison libssl-dev libelf-dev \
    rsync python3 device-tree-compiler
```

### 5. Build RT kernel + OOT modules

```bash
cd ~/Linux_for_Tegra/source
./nvbuild.sh -r
```

- `-r` enables PREEMPT_RT via `generic_rt_build.sh enable`, which sets:
  - `CONFIG_PREEMPT_RT=y`
  - `CONFIG_DEBUG_PREEMPT=n`
  - `CONFIG_KVM=n`
  - `CONFIG_FAIR_GROUP_SCHED=n`
  - plus other RT-required options
- Builds kernel Image, all in-tree modules, OOT modules (nvgpu, nvidia-oot,
  nvethernetrm, hwpm, nvdisplay), and DTBs
- Output goes to `kernel_out/`
- Build time: ~60–90 minutes natively on AGX Orin

### 6. Install

```bash
sudo ./nvbuild.sh -r -i
```

Installs kernel to `/boot/Image` and modules to `/lib/modules/5.15.148-rt-tegra/`.

### 7. Post-install

```bash
# Back up RT kernel image
sudo cp /boot/Image /boot/Image.rt

# Generate initrd for the RT kernel
sudo update-initramfs -c -k 5.15.148-rt-tegra
sudo cp /boot/initrd.img-5.15.148-rt-tegra /boot/initrd
```

The existing `/boot/extlinux/extlinux.conf` primary entry already points to
`/boot/Image` and `/boot/initrd` — no changes needed.

### 8. Reboot and verify

```bash
sudo reboot
```

After boot:

```bash
uname -r                  # 5.15.148-rt-tegra
cat /sys/kernel/realtime  # 1
```

---

## Testing with cyclictest

### Install

```bash
sudo apt install rt-tests
```

### Basic test (no load)

```bash
sudo cyclictest -p 99 -m -i 200 -q -D 60s
```

### With stress load (two terminals)

Terminal 1:
```bash
stress-ng --cpu 0 --io 2 --vm 1 --vm-bytes 256M
```

Terminal 2:
```bash
sudo cyclictest -p 99 -m -i 200 -q -D 60s -H 400 --histfile=/tmp/latency.hist
```

### Expected results on AGX Orin

| Condition | Max latency |
|-----------|-------------|
| Idle, no isolation | ~100–300 µs |
| Under load, no isolation | ~200–500 µs |
| With `isolcpus` | < 50 µs |

### Key options

| Option | Meaning |
|--------|---------|
| `-p 99` | SCHED_FIFO priority 99 (highest RT priority) |
| `-m` | Lock memory — prevents page fault latency spikes |
| `-i 200` | Wake up every 200 µs |
| `-q` | Quiet — only print summary at end |
| `-D 60s` | Run for 60 seconds |
| `-H 400` | Histogram up to 400 µs |

---

## Scheduling Policy Reference

| Policy | Scheduler | Time Slice | Use Case |
|--------|-----------|------------|----------|
| `fifo` | SCHED_FIFO | None — runs until it blocks | Hard RT tasks (CAN TX/RX, sensors) |
| `rr` | SCHED_RR | Fixed slice (~100ms) | Avoid for hard RT |
| `other` | CFS | Dynamic | Normal application logic |
| `batch` | CFS | Dynamic, CPU-bound bias | Background processing |
| `idle` | CFS | Lowest possible | Housekeeping only |

**Priority scale for SCHED_FIFO:** 1 (lowest) to 99 (highest). Higher number = higher
priority = preempts more = less latency.

### Setting RT priority in C

```c
#include <sched.h>
#include <pthread.h>

struct sched_param param = { .sched_priority = 90 };
pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
```

### Setting RT priority on a running process

```bash
chrt -f -p 90 <pid>
```

---

## Notes & Gotchas

- **nvdisplay extraction**: Always use `nvidia_kernel_display_driver_source.tbz2`
  (with root dir), not the `_without_root_dir` variant, when extracting into the
  source directory. The without-root-dir tarball overwrites the OOT `Makefile`.
- **sudo in background**: `nvbuild.sh -r -i` requires an interactive terminal for
  sudo. Run the install step directly in a shell, not in a background process.
- **fsck warning** during `update-initramfs` is harmless on Jetson.
- **OOT modules** are installed to `/lib/modules/5.15.148-rt-tegra/updates/` and
  include: `nvgpu.ko`, `nvidia.ko`, `nvidia-modeset.ko`, `nvidia-drm.ko`, and all
  other NVIDIA OOT drivers.
- **Further latency tuning**: Add `isolcpus=<cores> nohz_full=<cores> rcu_nocbs=<cores>`
  to the kernel command line in `/boot/extlinux/extlinux.conf` to isolate cores
  from the scheduler and reduce latency further.
