# STM32F407 Health Check

Firmware for a standalone network health-check device based on the
STM32F407VET6. The device monitors HTTPS resources, records their availability,
and exposes a secure management API for configuration and diagnostics.

The default configuration periodically sends an HTTP `HEAD` request to
`https://pgw.intraclear.com/`. A resource is considered healthy only when DNS
resolution, TCP connection, TLS 1.3 negotiation, certificate and hostname
validation, and the HTTP request all succeed with status `200`.

Project documentation:

- [Development rules](docs/DEVELOPMENT_RULES.md)
- [Naming conventions](docs/NAMING_CONVENTIONS.md)
- [Supplying ignored source trees in forks](docs/IGNORED_SOURCES.md)

## Features

- STM32F407VET6 running at 168 MHz from a 25 MHz HSE
- FreeRTOS with statically allocated application tasks
- lwIP 2.1.2 with a dedicated TCP/IP core thread
- Integrated Ethernet MAC, RMII, and DP83848 PHY
- DHCP with a static-address fallback and automatic recovery after link loss
- DNS for NTP, HTTPS health checks, and other network services
- LSE-backed RTC synchronized in UTC with `pool.ntp.org`
- Independent watchdog supervised by the default FreeRTOS task
- TLS 1.3 client and server based on Mbed TLS 3.6 LTS
- Periodic HTTPS health checks for up to three configurable resources
- Persistent, wear-aware health-check history in onboard NOR Flash
- Authenticated JSON management API over HTTPS
- Runtime management-server certificate and private-key replacement
- W25Q64JV NOR Flash interface on SPI2
- DS18B20 support for up to six sensors on the dedicated one-wire connector
- Diagnostic `printf()` output through the onboard RS485 interface
- Device-specific locally administered MAC address derived from the STM32 UID

## Runtime architecture

FreeRTOS runs with a 1 kHz tick and uses static allocation. Startup is divided
into two phases:

1. `main()` initializes the MCU, peripherals, and RTOS objects. It also allows
   2.5 seconds for the board's Ethernet PHY to stabilize before starting the
   scheduler.
2. A dedicated startup task initializes the W25Q64 Flash and persistent stores
   after the scheduler is running, then creates the application and network
   services. This keeps lengthy Flash operations out of the pre-scheduler
   startup path.

The principal services are:

- **Default task** — starts and refreshes the independent watchdog.
- **Network task** — initializes lwIP, drains Ethernet frames, monitors the PHY,
  and maintains DHCP or fallback addressing.
- **Time service** — synchronizes the hardware RTC with `pool.ntp.org` and
  periodically prints UTC time.
- **Temperature service** — discovers DS18B20 sensors and updates their readings
  every seven seconds.
- **Health-check service** — performs configured HTTPS checks and records the
  results.
- **Management API service** — accepts authenticated TLS 1.3 API requests on
  TCP port 443.

lwIP uses `NO_SYS=0` and a native FreeRTOS system port. Ethernet frames are
handled by the network task, while protocol processing and raw API callbacks
run in lwIP's TCP/IP core thread.

SysTick remains the STM32 HAL timebase. Its interrupt increments the HAL tick
and, after the scheduler starts, also dispatches the FreeRTOS tick. FreeRTOS
provides the SVC and PendSV handlers through its Cortex-M4F port.

The watchdog uses the LSI clock, prescaler 256, and reload value 4095, giving a
nominal timeout of approximately 32 seconds. It starts only after the scheduler
is operational, so the mandatory PHY startup delay cannot consume its window.

## Network configuration

DHCP starts after the network interface and PHY initialize successfully. When
the link is up but DHCP does not assign an address within ten seconds, the
firmware uses:

- IP address: `192.168.0.50`
- Network mask: `255.255.255.0`
- Gateway: `192.168.0.1`
- DNS server: `8.8.8.8`

The active IP address, mask, gateway, and DNS server are printed whenever the
network configuration changes. Link loss takes the interface down without
discarding its configuration. When the cable is reconnected, the MAC restarts
with the PHY's negotiated speed and duplex, and DHCP is attempted again.

The Ethernet driver uses blocking transmission with a bounded 20 ms timeout.
Receive buffers and DMA descriptors remain in ordinary SRAM because Ethernet
DMA cannot access CCM RAM.

## HTTPS health checks

The health-check service waits for a usable network address and NTP-synchronized
RTC. It then checks each enabled resource sequentially. A check opens the
configured TCP port, establishes TLS 1.3 with mandatory certificate and
hostname validation, and sends:

```http
HEAD <path> HTTP/1.1
Host: <host>
Connection: close
```

Only the bounded HTTP status line is read; no response body is requested or
stored. Up to three resources can be configured. Each enabled resource is
checked once per configured period, from 60 through 1800 seconds. The default
period is 60 seconds and a freshly provisioned device contains one resource:
`https://pgw.intraclear.com/`.

The embedded trust store currently contains the USERTrust RSA Certification
Authority required by the default target. Every configured resource must
present a chain anchored by this trust store. Review or replace the trust
anchor when changing targets or when their certificate chains change. Correct
RTC time is required for certificate validation.

TLS obtains entropy from the STM32 hardware random-number generator. Its
dedicated 52 KiB allocator arena resides in CPU-only CCM RAM, preserving
ordinary SRAM for FreeRTOS, lwIP, and Ethernet DMA. The transport layer is
kept independent of STM32F407 peripherals to simplify future STM32F767 and
STM32F769 ports.

### Diagnostics

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

The `stage` identifies the failed layer:

| Stage | Meaning |
|------:|---------|
| `0` | Transport completed successfully. |
| `1` | DNS lookup failed. |
| `2` | TCP socket creation or connection failed. |
| `3` | Local TLS configuration, entropy, allocation, or request construction failed. |
| `4` | Trust-anchor parsing or peer-certificate validation failed. |
| `5` | The TLS 1.3 handshake failed for a reason other than certificate validation. |
| `6` | Encrypted request transmission failed. |
| `7` | The HTTP status line could not be received or parsed. |

The `detail` value depends on the stage:

- Stage `1` reports the lwIP `getaddrinfo()` result.
- Stage `2` reports a negative socket `errno`. Typical values include `-12`
  for insufficient lwIP memory, `-100` for a down network, `-101` for an
  unreachable network or gateway, `-105` for an lwIP buffer or source-port
  allocation failure, `-110` for a timeout, and `-111` for a refused
  connection.
- Stages `3` through `7` normally report a negative Mbed TLS error. Socket I/O
  failures preserve their negative `errno`, and socket timeouts use
  `MBEDTLS_ERR_SSL_TIMEOUT`.

An HTTP error such as `404` or `503` still completes transport with `stage=0`
and `detail=0`; the received status is printed, but the final health verdict is
`FAILED`. A zero detail value means the completed operation supplied no more
specific lower-level error.

### Persistent result log

Every completed check is appended to a Flash-backed ring. The management API
returns the ten newest records through `GET /api/v1/health-check/logs`:

```json
{"logs":[{"sequence":123456,"timestamp":1785500000,"resource_index":0,
"status":"ok","http_status":200,"elapsed_ms":845,"detail":0}]}
```

`resource_index` identifies the configured slot at the time of the check.
`status` uses the diagnostic stages (`ok`, `dns_error`, `connect_error`,
`config_error`, `certificate_error`, `handshake_error`, `io_error`, or
`protocol_error`). `detail` has the same meaning as in the console diagnostic.
The monotonically increasing `sequence` remains the reliable ordering key when
an early record was written before RTC synchronization.

## Management API

The device serves a bounded JSON API over TLS 1.3 on TCP port 443. Except for
the two binary credential-upload endpoints, all request and response bodies,
including errors, use JSON.

### Initial server certificate

Generate a self-signed ECDSA P-256 certificate and private key before building:

```sh
tools/generate_server_certificate.sh health-check.local 192.168.0.50
```

Generated credentials are stored under the ignored `TLS/Private/` directory.
The private key must never be committed. Regenerate the certificate with the
device's intended DNS name and fallback IP before deployment, and explicitly
trust the certificate or its SHA-256 fingerprint in the API client.

### Authentication and sessions

The built-in administrator username is `master`. Its plaintext password is not
stored. Firmware contains only a random salt and a PBKDF2-HMAC-SHA-256 verifier
using 100,000 iterations. Password comparison is constant-time and temporary
derived material is cleared after use.

Generate or rotate the master verifier from the repository root:

```sh
python3 tools/generate_master_verifier.py
```

The tool reads and confirms the password without echoing it, then replaces
`Core/Inc/master_password_credentials.h`. Rebuild and reflash the firmware
after rotation.

Additional users are stored in NOR Flash as salted password verifiers. The
`master` account cannot be created or deleted through the API. Every account
has at most one in-memory session: login or refresh issues a new access/refresh
pair and invalidates the previous pair. Only SHA-256 token digests are retained.
Access tokens expire after 15 minutes, refresh tokens after seven days, and all
sessions are lost on reset. Updating or deleting a user revokes that user's
active session.

For a production device, enable STM32 readout protection Level 1 only after
final programming and verification. Do not enable irreversible Level 2 during
development.

### Endpoints

| Method | Endpoint | Authorization | Purpose |
|--------|----------|---------------|---------|
| `POST` | `/api/v1/auth/token` | None | Exchange a username and password for access and refresh tokens. |
| `POST` | `/api/v1/auth/refresh` | Refresh token in JSON | Rotate both tokens. |
| `POST` | `/api/v1/auth/revoke` | Bearer | Revoke the active session. |
| `GET` | `/api/v1/users` | Administrator bearer | List users without password material. |
| `POST` | `/api/v1/users` | Administrator bearer | Create a user. |
| `PUT` | `/api/v1/users/{username}` | Administrator bearer | Replace the password and optionally the role or enabled state. |
| `DELETE` | `/api/v1/users/{username}` | Administrator bearer | Delete a user and revoke its session. |
| `PUT` | `/api/v1/tls/certificate` | Administrator bearer | Upload a raw DER server certificate. |
| `PUT` | `/api/v1/tls/private-key` | Administrator bearer | Upload a raw DER server private key. |
| `GET` | `/api/v1/health-check/config` | Administrator bearer | Read the period and configured resources. |
| `PUT` | `/api/v1/health-check/config` | Administrator bearer | Set the period from 60 through 1800 seconds. |
| `POST` | `/api/v1/health-check/resources` | Administrator bearer | Add a resource; up to three slots are available. |
| `PUT` | `/api/v1/health-check/resources/{index}` | Administrator bearer | Update a resource; omitted fields retain their values. |
| `DELETE` | `/api/v1/health-check/resources/{index}` | Administrator bearer | Clear a resource slot; a later resource may reuse its index. |
| `GET` | `/api/v1/health-check/logs` | Administrator bearer | Return the ten newest completed checks. |
| `GET` | `/api/v1/temperature` | Any authenticated bearer | Return the latest DS18B20 readings. |
| `GET` | `/api/v1/rtc` | Any authenticated bearer | Return UTC time and synchronization state. |

Passwords must contain 12 through 128 bytes, usernames may contain at most 24
bytes, and all requests are deliberately bounded to protect MCU memory.

### Updating the server certificate and key

The management server can replace its certificate and private key without a
reboot. Convert a PEM pair to DER:

```sh
python3 tools/convert_credentials_to_der.py
```

Upload the raw DER files without JSON wrapping:

```sh
curl -sk -X PUT https://<device>/api/v1/tls/certificate \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/octet-stream" \
  --data-binary @TLS/Private/management_server.crt.der
curl -sk -X PUT https://<device>/api/v1/tls/private-key \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/octet-stream" \
  --data-binary @TLS/Private/management_server.key.der
```

Each upload is parsed and staged. Once a matching certificate and key are
available, the pair is verified and committed transactionally to Flash. The
current connection continues with the credential it presented during its
handshake; subsequent connections use the newly activated pair.

`{"status":"activated"}` confirms activation. A mismatched pair returns
`409 key_mismatch` and is not committed. A pending response identifies the
missing counterpart. Until a complete valid replacement is activated, the
currently active credential remains in service.

### Postman tests

Import
`test/postman/STM32_F407_Health_Check_API.postman_collection.json`. Set these
collection variables locally:

- `baseUrl` — the device URL, updated for its DHCP address if necessary
- `masterPassword` and `testPassword` — secret test credentials
- `certificateDerPath` and `privateKeyDerPath` — generated DER files

Trust the management certificate in Postman. The collection tests
authentication and token rotation, user CRUD, RTC and temperature reads,
health-check configuration and logs, resource CRUD, credential replacement,
and token revocation. Scripts automatically retain rotated tokens and the
resource index created during the run. Depending on the Postman version, raw
binary upload files may still need to be selected manually.

## Onboard NOR Flash

The JZ-F407VET6 carries an 8 MiB Winbond W25Q64JV connected to SPI2:

- `PB10` — SCK
- `PC2` — MISO
- `PC3` — MOSI
- `PE3` — software-controlled chip select

SPI2 uses mode 0 at 10.5 MHz. Chip select is driven high before SPI
initialization so the device remains deselected during startup.
`Periph/Inc/flash_layout.h` defines persistent sectors from the top of Flash:

| Store | Sectors | Update pattern |
|---|---:|---|
| Management users | 2 | A/B transactional snapshot on user changes. |
| Health-check configuration | 2 | A/B transactional snapshot on administrator changes. |
| TLS server credential | 2 | A/B transactional snapshot on credential changes. |
| Health-check result log | 2 | Wear-aware append-only ring. |

An A/B store writes and verifies a complete snapshot in the inactive sector
before making it active, protecting infrequently changed data from power loss
during an update.

The result log changes much more often. With three resources enabled, as many
as three records may be written during each 60-second cycle. Records are
therefore appended into erased locations, and a sector is erased only after it
is full and later reused. This distributes erase cycles and avoids the rapid
wear caused by rewriting a whole sector for every result.

## Temperature sensor

The `P7:18B20` connector routes one-wire data to `PE2`. The driver discovers up
to six DS18B20 sensors, validates ROM and scratchpad CRC values, and supports
external or parasitic power. The service converts sensors sequentially every
seven seconds and rescans the bus once per minute.

## RS485 diagnostic output

`printf()` is temporarily routed to the onboard RS485 interface. Connect the
matching `A` and `B` terminals of an RS485-to-USB adapter and use:

- 115200 baud
- 8 data bits
- no parity
- 1 stop bit
- no flow control

USART2 uses `PD5`/`PD6`, with `PD7` controlling transceiver direction. The
interface is transmit-only and intended for development diagnostics.

## Memory

The STM32F407VET6 provides 512 KiB internal Flash, 128 KiB ordinary SRAM, and
64 KiB CPU-only CCM RAM. The current build uses approximately 321 KiB of
Flash, 100 KiB of ordinary static SRAM, and a 52 KiB CCM allocation arena for
Mbed TLS.

The linker exposes `.ccmram` for CPU-only working memory. Ethernet descriptors,
packet buffers, and every other DMA target must remain in ordinary SRAM.

## Project structure

- `Core/` — application startup, HAL configuration, and exception handlers
- `Periph/` — board peripheral drivers
- `Srv/` — FreeRTOS application and network services
- `FreeRTOS-Kernel/` — imported kernel and Cortex-M4F port
- `LWIP/App/` — application-level lwIP initialization
- `LWIP/Target/` — Ethernet MAC and DP83848 adaptation
- `TLS/` — platform adaptation, trust store, and HTTPS transport
- `Drivers/` — ST HAL, CMSIS, and PHY vendor sources
- `Middlewares/` — imported lwIP and Mbed TLS source distributions
- `tools/` — credential-generation and conversion utilities
- `test/postman/` — management API integration collection

Vendor source trees remain intact. The Makefile selects only the modules used
by the firmware.

## Build

Install the GNU Arm Embedded toolchain, supply the ignored source trees as
described in [Supplying ignored source trees in forks](docs/IGNORED_SOURCES.md),
and run:

```sh
git submodule update --init --recursive
make clean
make -j4
```

ELF, HEX, BIN, map, dependency, and listing files are generated under
`build/`. Mbed TLS is pinned to the 3.6 LTS line.

---

&copy; 2017-2026 Askug Ltd., Dmitry Slobodchikov
