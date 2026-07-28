# STM32F407 Health Check

Firmware platform for a standalone network health-check device based on the
STM32F407VET6. The long-term goal is to monitor an Internet resource over
HTTPS, evaluate its HTTP response status, retain diagnostic history, and
report failures locally.

This branch establishes the Ethernet and timekeeping baseline. HTTPS,
FreeRTOS, persistent logging, and application health policy are intentionally
left for later issues.

## Current functionality

- STM32F407 running at 168 MHz from a 25 MHz HSE
- LSE-backed hardware RTC initialization
- Hardware CRC initialization
- Integrated STM32 Ethernet MAC in RMII mode
- DP83848 Ethernet PHY
- Bare-metal lwIP 2.1.2 polling
- IPv4 address acquisition through DHCP
- DNS client support for future network services
- Periodic PHY link monitoring
- Zero-copy Ethernet receive buffers
- Device-specific locally administered MAC address derived from the STM32 UID

## Network architecture

lwIP currently runs with `NO_SYS=1`; there is no RTOS or lwIP TCP/IP thread.
The main loop must call `Lwip_Process()` continuously. That function drains
received Ethernet frames, advances lwIP protocol timers, and checks PHY link
state every 100 ms.

The Ethernet driver uses blocking transmission with a bounded 20 ms timeout.
Transmit failures are returned to lwIP. Receive buffers and DMA descriptors
remain in the normal 128 KB SRAM because the Ethernet DMA controller cannot
access the 64 KB CCM RAM.

DHCP begins after the network interface and PHY initialize successfully. Link
loss leaves the interface configured but down; link recovery restarts the MAC
with the speed and duplex reported by the PHY.

## Memory

The STM32F407VET6 provides:

- 512 KB internal flash
- 128 KB ordinary SRAM accessible by Ethernet DMA
- 64 KB core-coupled RAM accessible only by the CPU

The linker exposes a `.ccmram` section for explicitly initialized CPU-only
working memory. Do not place Ethernet descriptors, packet buffers, or other
DMA targets in that section.

## Project structure

- `Core/` — application startup, HAL configuration, and exception handlers
- `LWIP/App/` — application-level lwIP initialization and polling
- `LWIP/Target/` — STM32 Ethernet MAC and DP83848 adaptation
- `Drivers/` — ST HAL, CMSIS, and PHY vendor sources
- `Middlewares/` — complete imported lwIP source distribution

Vendor source trees are retained intact. The Makefile selects only the HAL and
lwIP modules required by the current firmware.

## Build

Use the GNU Arm Embedded toolchain:

```sh
make clean
make -j4
```

ELF, HEX, BIN, map, dependency, and listing files are generated under
`build/`.

---

&copy; 2017-2026 Askug Ltd., Dmitry Slobodchikov
