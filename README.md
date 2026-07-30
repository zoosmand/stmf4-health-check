# STM32F407 Health Check

Firmware platform for a standalone network health-check device based on the
STM32F407VET6. The long-term goal is to monitor an Internet resource over
HTTPS, evaluate its HTTP response status, retain diagnostic history, and
report failures locally.

The firmware periodically performs an authenticated TLS 1.3 connection and
an HTTP `HEAD` request to `https://pgw.intraclear.com/`. The resource is
healthy only when certificate and hostname validation succeed and the server
returns HTTP status `200`.

## Current functionality

- STM32F407 running at 168 MHz from a 25 MHz HSE
- LSE-backed hardware RTC synchronized in UTC with `pool.ntp.org`
- IWDG initialized by the default task and refreshed once per second
- Hardware CRC initialization
- Onboard W25Q64JV NOR Flash interface on SPI2
- DS18B20 support on the dedicated one-wire socket
- Integrated STM32 Ethernet MAC in RMII mode
- DP83848 Ethernet PHY
- FreeRTOS with statically allocated application tasks
- FreeRTOS-aware lwIP 2.1.2 with a dedicated TCP/IP core thread
- IPv4 address acquisition through DHCP
- DNS client support for future network services
- Periodic PHY link monitoring
- Zero-copy Ethernet receive buffers
- Device-specific locally administered MAC address derived from the STM32 UID
- Standard output through the onboard RS485 interface
- Authenticated TLS 1.3 client based on Mbed TLS 3.6 LTS
- Periodic HTTPS `HEAD` resource health check

## RTOS and network architecture

FreeRTOS uses a 1 kHz tick and static allocation only. The default task is a
placeholder for future application coordination. A higher-priority network
task calls `Lwip_Process()` every millisecond.

lwIP is configured with `NO_SYS=0` and a statically allocated native FreeRTOS
system port. The network task drains Ethernet frames and checks PHY state,
while protocol processing and raw API callbacks run in lwIP's dedicated
TCP/IP core thread.

SysTick remains the STM32 HAL timebase. The interrupt handler increments the
HAL tick and dispatches the FreeRTOS tick after the scheduler has started.
FreeRTOS supplies the SVC and PendSV exception handlers through its Cortex-M4F
port.

The confirmed 2.5-second PHY stabilization delay runs before the scheduler
starts. The network task then initializes lwIP and handles DHCP negotiation.

The time service resolves `pool.ntp.org`, sends a compact UDP NTP request, and
sets the LSE-backed RTC from the returned UTC timestamp. It retries failures
after 60 seconds, refreshes synchronization hourly, and prints synchronization
status plus the current UTC time through RS485 `printf()`.

The independent watchdog starts from the default task after the scheduler is
operational. With the LSI clock, prescaler 256, and reload value 4095, its
nominal timeout is approximately 32 seconds. Starting it after Ethernet
initialization prevents the board-required PHY delay from consuming the
watchdog window.

## HTTPS health check

The health-check service waits until Ethernet is ready and the RTC has been
synchronized by NTP. It then resolves `pgw.intraclear.com`, opens TCP port
443, and establishes TLS 1.3 with mandatory certificate and hostname
validation. The client sends:

```http
HEAD / HTTP/1.1
Host: pgw.intraclear.com
Connection: close
```

Only the bounded HTTP status line is read; the response body is neither
requested nor retained. A check runs once per minute and reports the TLS
version, cipher suite, HTTP status, elapsed time, and final health verdict
through `printf()`.

The firmware trusts the USERTrust RSA Certification Authority used by the
target server's current certificate chain. The embedded trust anchor must be
reviewed whenever the target changes its certification chain and before the
root expires. Correct RTC time is a security requirement, not merely a
logging convenience.

TLS uses the STM32 hardware random-number generator. A dedicated 52 KB Mbed
TLS allocation arena resides in CPU-only CCM RAM, keeping ordinary SRAM
available to FreeRTOS, lwIP, and Ethernet DMA. The transport layer is kept
independent of STM32F407 peripheral details to ease migration to STM32F767
and STM32F769.

### Health-check diagnostics

A successful check resembles:

```text
HTTPS check: https://pgw.intraclear.com
TLS: TLSv1.3, <cipher suite>, certificate valid
HTTP HEAD: 200, <elapsed time> ms
Resource health: OK
```

Failures are reported as:

```text
HTTPS failure: stage=<stage>, detail=<detail>, <elapsed time> ms
Resource health: FAILED
```

The `stage` value identifies the failed layer:

| Stage | Meaning |
|------:|---------|
| `0` | Transport completed successfully. |
| `1` | DNS lookup failed. |
| `2` | TCP socket creation or connection failed. |
| `3` | Local TLS configuration, entropy, allocation, or request construction failed. |
| `4` | Trust-anchor parsing or peer-certificate validation failed. |
| `5` | TLS 1.3 handshake failed for a reason other than certificate validation. |
| `6` | Encrypted request transmission failed. |
| `7` | The HTTP status line could not be received or parsed. |

The meaning of `detail` depends on the stage:

- Stage `1` contains the lwIP `getaddrinfo()` result.
- Stage `2` contains a negative socket `errno`. Common values include `-12`
  for insufficient lwIP memory, `-100` for a down network, `-101` for an
  unreachable network or gateway, `-105` for an internal lwIP buffer or
  source-port allocation failure, `-110` for a timeout, and `-111` for a
  refused connection.
- Stages `3` through `7` normally contain a negative Mbed TLS error code.
  The exception is an HTTP status such as `404` or `503`: transport still
  completes with stage `0`, `detail=0`, and the received status is shown by
  `HTTP HEAD`; the final resource verdict is nevertheless `FAILED`.

`detail=0` means that the completed operation supplied no lower-level error.
The numeric detail is intended for diagnosis and must not replace the final
health verdict.

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
- `TLS/` — platform adaptation, trust store, and HTTPS transport
- `Middlewares/` — imported lwIP and Mbed TLS source distributions

Vendor source trees are retained intact. The Makefile selects only the HAL and
lwIP modules required by the current firmware.

## Build

Use the GNU Arm Embedded toolchain:

```sh
git submodule update --init --recursive
make clean
make -j4
```

ELF, HEX, BIN, map, dependency, and listing files are generated under
`build/`.

The Mbed TLS submodule is pinned to the 3.6 LTS line. The current build uses
approximately 298 KB of Flash, 76 KB of ordinary SRAM, and 52 KB of CCM RAM.

---

&copy; 2017-2026 Askug Ltd., Dmitry Slobodchikov
