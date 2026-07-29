# STM32F407 Health Check

Firmware platform for a standalone network health-check device based on the
STM32F407VET6. The long-term goal is to monitor an Internet resource over
HTTPS, evaluate its HTTP response status, retain diagnostic history, and
report failures locally.

This branch establishes the Ethernet, timekeeping, and FreeRTOS platform
baseline. HTTPS, persistent logging, and application health policy are
intentionally left for later issues.

## Current functionality

- STM32F407 running at 168 MHz from a 25 MHz HSE
- LSE-backed hardware RTC initialization
- IWDG initialized by the default task and refreshed once per second
- Hardware CRC initialization
- Onboard W25Q64JV NOR Flash interface on SPI2
- DS18B20 support on the dedicated one-wire socket
- Integrated STM32 Ethernet MAC in RMII mode
- DP83848 Ethernet PHY
- FreeRTOS with statically allocated application tasks
- Raw lwIP 2.1.2 polling from a dedicated network task
- IPv4 address acquisition through DHCP
- DNS client support for future network services
- Periodic PHY link monitoring
- Zero-copy Ethernet receive buffers
- Device-specific locally administered MAC address derived from the STM32 UID
- Standard output through the onboard RS485 interface

## RTOS and network architecture

FreeRTOS uses a 1 kHz tick and static allocation only. The default task is a
placeholder for future application coordination. A higher-priority network
task calls `Lwip_Process()` every millisecond.

lwIP remains configured with `NO_SYS=1`; there is no lwIP TCP/IP thread.
Keeping every raw lwIP call in the network task preserves the required
single-context execution model. `Lwip_Process()` drains received Ethernet
frames, advances protocol timers, and checks PHY link state every 100 ms.

SysTick remains the STM32 HAL timebase. The interrupt handler increments the
HAL tick and dispatches the FreeRTOS tick after the scheduler has started.
FreeRTOS supplies the SVC and PendSV exception handlers through its Cortex-M4F
port.

The confirmed 2.5-second PHY stabilization delay and lwIP initialization run
before the scheduler starts. DHCP negotiation then advances from the network
task.

The independent watchdog starts from the default task after the scheduler is
operational. With the LSI clock, prescaler 256, and reload value 4095, its
nominal timeout is approximately 32 seconds. Starting it after Ethernet
initialization prevents the board-required PHY delay from consuming the
watchdog window.

## Onboard NOR Flash

The JZ-F407VET6 carries an 8 MB Winbond W25Q64JV connected to SPI2:

- `PB10` — SPI2 SCK
- `PC2` — SPI2 MISO
- `PC3` — SPI2 MOSI
- `PE3` — software-controlled Flash chip select

SPI2 is initialized in mode 0 with an APB1-derived 10.5 MHz clock. Chip select
is driven high before SPI initialization so the Flash remains deselected
during startup. The storage driver and read/write policy will be added
separately.

## Temperature sensor

The dedicated `P7:18B20` connector routes its one-wire data signal to `PE2`.
The driver discovers up to six DS18B20 devices, validates ROM and scratchpad
CRC values, and supports both externally powered and parasitic-powered
sensors. A FreeRTOS service refreshes measurements every seven seconds and
rescans the bus once per minute.

## Temporary standard output over RS485

The standard `printf()` output is routed to the board's onboard RS485
interface. Connect an RS485-to-USB adapter to the `A` and `B` terminals and
open its serial port with the following settings:

- 115200 baud
- 8 data bits
- no parity
- 1 stop bit
- no flow control

The firmware uses USART2 on `PD5`/`PD6` and `PD7` for transceiver direction
control. The interface is currently intended for transmit-only diagnostic
output. It reports each DS18B20 conversion every seven seconds and prints the
active Ethernet address, mask, gateway, and primary DNS server when network
configuration completes.

DHCP is attempted first. If no address is assigned within ten seconds while
the Ethernet link is up, the firmware applies this fallback configuration:

- IP address: `192.168.0.50`
- Network mask: `255.255.255.0`
- Gateway: `192.168.0.1`
- DNS server: `8.8.8.8`

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
- `Srv/` — FreeRTOS application and network services
- `FreeRTOS-Kernel/` — imported FreeRTOS kernel and Cortex-M4F port
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
