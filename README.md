# STM32F407 Health Check

Firmware platform for a standalone network health-check device based on the
STM32F407VET6. The long-term goal is to monitor an Internet resource over
HTTPS, evaluate its HTTP response status, retain diagnostic history, and
report failures locally.

Project documentation:

- [Development rules](docs/DEVELOPMENT_RULES.md)
- [Naming conventions](docs/NAMING_CONVENTIONS.md)
- [Supplying ignored source trees in forks](docs/IGNORED_SOURCES.md)

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
- Periodic HTTPS `HEAD` health check against up to three configurable
  resources, with a persistent, wear-aware result log
- Bounded HTTPS JSON management API: user CRUD, runtime TLS certificate/key
  updates, health-check configuration, and temperature/RTC readouts

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

## Management authentication

The management API authentication foundation stores no plaintext master
password. Firmware contains a 16-byte random salt and a 32-byte
PBKDF2-HMAC-SHA-256 verifier in internal Flash. Password verification performs
100,000 derivation iterations, compares the result in constant time, clears the
temporary derived value, and serializes access to the shared Mbed TLS allocator.

Generate a new verifier from the repository root:

```sh
python3 tools/generate_master_verifier.py
```

The tool reads and confirms the password without echoing it, generates a fresh
salt, and replaces `Core/Inc/master_password_credentials.h`. Only the salt,
iteration count, and verifier are written; the plaintext password is not
retained. Rebuild and reflash the firmware after rotating the password.

For a production device, enable STM32 readout protection Level 1 after final
programming and verification. Do not enable irreversible Level 2 during
development. The management API must accept the master password only through
authenticated HTTPS and use it to issue a short-lived bearer token; bearer
token handling will be added with the API server.

Generate a local ECDSA P-256 certificate and private key for the HTTPS
management server:

```sh
tools/generate_server_certificate.sh health-check.local 192.168.0.50
```

The generated files are written under the ignored `TLS/Private/` directory.
The private key must never be committed. The default certificate identifies
`health-check.local` and the fallback IP address; regenerate it with the
device's intended DNS name and static IP before deployment. A client must
explicitly trust the self-signed certificate or its SHA-256 fingerprint.

Import
`test/postman/STM32_F407_Health_Check_API.postman_collection.json` into
Postman to test login, token rotation, revocation, and administrator user
management. Set the secret `masterPassword` and `testPassword` collection
variables locally and configure Postman to trust the generated certificate.
The collection automatically replaces its stored access and refresh tokens
after login and refresh.

## HTTPS health check

The health-check service waits until Ethernet is ready and the RTC has been
synchronized by NTP. It then checks up to three independently configured
resources, each in turn, opening TCP port 443 and establishing TLS 1.3 with
mandatory certificate and hostname validation. For each resource the client
sends:

```http
HEAD <path> HTTP/1.1
Host: <host>
Connection: close
```

Only the bounded HTTP status line is read; the response body is neither
requested nor retained. All enabled resources are checked once per
configured period (60 through 1800 seconds, 60 by default) and each reports
its TLS version, cipher suite, HTTP status, elapsed time, and final health
verdict through `printf()`. The target host, port, path, period, and each
resource's enabled state are managed through the [management API](#management-api)
and persist in Flash; a freshly provisioned device defaults to a single
resource matching the original hardcoded target
(`https://pgw.intraclear.com/`).

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
  A socket I/O failure instead preserves its negative `errno`, while an
  expired socket timeout is reported as `MBEDTLS_ERR_SSL_TIMEOUT`. The other
  exception is an HTTP status such as `404` or `503`: transport still
  completes with stage `0`, `detail=0`, and the received status is shown by
  `HTTP HEAD`; the final resource verdict is nevertheless `FAILED`.

`detail=0` means that the completed operation supplied no lower-level error.
The numeric detail is intended for diagnosis and must not replace the final
health verdict.

Every completed check, across all configured resources, also appends a
timestamped record to a persistent ring in Flash. The ten most recent
records, newest first, are readable through `GET /api/v1/health-check/logs`;
see [Health-check result log](#health-check-result-log).

## Onboard NOR Flash

The JZ-F407VET6 carries an 8 MB Winbond W25Q64JV connected to SPI2:

- `PB10` — SPI2 SCK
- `PC2` — SPI2 MISO
- `PC3` — SPI2 MOSI
- `PE3` — software-controlled Flash chip select

SPI2 is initialized in mode 0 with an APB1-derived 10.5 MHz clock. Chip select
is driven high before SPI initialization so the Flash remains deselected
during startup. `Periph/Inc/flash_layout.h` centralizes every persistent
store's sector allocation, counting down from the top of the chip:

| Store | Sectors | Update pattern |
|---|---|---|
| Management users | last 2 | A/B transactional; rewritten whenever a user changes. |
| Health-check config (period + resources) | next 2 | A/B transactional; rewritten only on admin changes. |
| TLS server certificate/key | next 2 | A/B transactional; rewritten only when the credential is updated. |
| Health-check result log | next 2 | Wear-aware append-only ring; see below. |

Management users are represented only by salted PBKDF2-HMAC-SHA-256
verifiers; plaintext passwords are never stored. The A/B pattern rewrites the
whole snapshot to the inactive sector, verifies the write, and only then
switches which sector is active — safe against power loss mid-write, and
appropriate for stores that change rarely (administrator actions only).

The health-check result log changes far more often — potentially every
check, as frequently as every 20 seconds at the minimum configured period
with three resources enabled — so it cannot use the same whole-sector
rewrite pattern without exhausting the Flash's rated erase-cycle life in
weeks. Instead, records are appended into successive, previously erased
offsets within the active sector (NOR Flash allows this as long as the same
bytes are never reprogrammed), and a sector is only erased when it is
completely full and its turn to be reused comes back around. At the
worst-case write rate this gives each sector on the order of two decades of
life against the chip's rated 100,000-erase-cycle floor.

## Management API

The device exposes a bounded JSON API over TLS 1.3 on TCP port 443. Generate
the local ECDSA P-256 server certificate and its ignored embedded header
before building:

```sh
./tools/generate_server_certificate.sh
```

Import `TLS/Private/management_server.crt.pem` into the client trust store.
The private key and generated credential header remain under `TLS/Private/`
and must never be committed.

| Method | Endpoint | Authorization | Purpose |
|--------|----------|---------------|---------|
| `POST` | `/api/v1/auth/token` | None | Exchange username/password for an access and refresh token. |
| `POST` | `/api/v1/auth/refresh` | Refresh token in JSON | Rotate both tokens. |
| `POST` | `/api/v1/auth/revoke` | Bearer | Revoke the active session. |
| `GET` | `/api/v1/users` | Administrator bearer | List users without password material. |
| `POST` | `/api/v1/users` | Administrator bearer | Create a user. |
| `PUT` | `/api/v1/users/{username}` | Administrator bearer | Replace password and optionally role/enabled state. |
| `DELETE` | `/api/v1/users/{username}` | Administrator bearer | Remove a user and revoke its active session. |
| `PUT` | `/api/v1/tls/certificate` | Administrator bearer | Upload a raw DER server certificate; see [Updating the TLS certificate and key](#updating-the-tls-certificate-and-key). |
| `PUT` | `/api/v1/tls/private-key` | Administrator bearer | Upload a raw DER server private key; see below. |
| `GET` | `/api/v1/health-check/config` | Administrator bearer | Read the check period and configured resources. |
| `PUT` | `/api/v1/health-check/config` | Administrator bearer | Set the check period, 60 through 1800 seconds. |
| `POST` | `/api/v1/health-check/resources` | Administrator bearer | Add a resource (host, port, path, enabled); up to 3. |
| `PUT` | `/api/v1/health-check/resources/{index}` | Administrator bearer | Replace a resource's fields; unspecified fields keep their current value. |
| `DELETE` | `/api/v1/health-check/resources/{index}` | Administrator bearer | Clear a resource slot. The slot index is never reassigned to a different resource. |
| `GET` | `/api/v1/health-check/logs` | Administrator bearer | The ten most recent check results, newest first; see [Health-check result log](#health-check-result-log). |
| `GET` | `/api/v1/temperature` | Any authenticated bearer | The latest DS18B20 readings. |
| `GET` | `/api/v1/rtc` | Any authenticated bearer | The current RTC UTC time and synchronization state. |

The built-in administrator username is `master` and cannot be deleted or
created through the API. Every account has exactly one in-memory session: a
successful login or refresh creates a new token pair and invalidates the old
pair. Only SHA-256 token digests are retained. Access tokens expire after 15
minutes, refresh tokens after seven days, and all sessions disappear on
reset. Updating or deleting a user also revokes that user's active session.

Every request body is JSON, and every response, including errors, is JSON —
**except** the two TLS credential upload endpoints, which take the raw DER
bytes directly as the request body (`Content-Type: application/octet-stream`,
no wrapping JSON). Passwords must contain 12 through 128 bytes. Usernames may
contain at most 24 bytes. Requests are deliberately bounded to protect MCU
memory.

### Updating the TLS certificate and key

The management server's own certificate and private key can be replaced at
runtime without a reboot. Convert an existing PEM pair (for example, the one
`tools/generate_server_certificate.sh` already produced) to DER:

```sh
python3 tools/convert_credentials_to_der.py
```

Then upload each half separately as a raw binary body:

```sh
curl -sk -X PUT https://<device>/api/v1/tls/certificate \
  -H "Authorization: Bearer $TOKEN" \
  --data-binary @TLS/Private/management_server.crt.der
curl -sk -X PUT https://<device>/api/v1/tls/private-key \
  -H "Authorization: Bearer $TOKEN" \
  --data-binary @TLS/Private/management_server.key.der
```

Each upload is parsed and, once both the certificate and a matching private
key are available — either just uploaded or already active — verified as a
pair before anything is committed to Flash. A response of
`{"status":"activated"}` means the new credential is already live: the next
TLS connection accepted by the management API (including the one carrying
that very response, on later connections) presents it. A mismatched pair
returns `409 key_mismatch` and neither half is committed, so uploading only a
renewed certificate that still matches the currently active key activates
immediately in one call. `{"status":"pending","awaiting":"private_key"}` (or
`"certificate"`) means the upload was accepted but is waiting for its
counterpart. Until a valid pair exists, the compiled-in certificate and key
from `TLS/Private/management_server_credentials.h` remain in use.

### Health-check result log

`GET /api/v1/health-check/logs` returns the ten most recent completed
checks, newest first, across every configured resource:

```json
{"logs":[{"sequence":123456,"timestamp":1785500000,"resource_index":0,
"status":"ok","http_status":200,"elapsed_ms":845,"detail":0}]}
```

`resource_index` matches the `index` field from
`GET /api/v1/health-check/config`. `status` uses the same stage vocabulary as
the [health-check diagnostics](#health-check-diagnostics) `stage` values
(`ok`, `dns_error`, `connect_error`, `config_error`, `certificate_error`,
`handshake_error`, `io_error`, `protocol_error`); `detail` is the same
layer-specific diagnostic code described there. `sequence` increases
monotonically and is never reused, even across the log's internal
sector rollovers, so it is a reliable ordering key independent of
`timestamp` if the RTC has not yet synchronized when an early record was
written.

For interactive testing, import
`test/postman/STM32_F407_Health_Check_API.postman_collection.json` into
Postman. Set the collection's secret `masterPassword` and `testPassword`
variables locally, adjust `baseUrl` to the DHCP address if necessary, and
trust the generated certificate. The collection captures rotated access and
refresh tokens automatically.

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
