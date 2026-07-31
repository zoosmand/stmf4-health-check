# Naming conventions

This document defines the preferred naming style for project-owned STM32F407
health-check firmware in `Core`, `Srv`, `LWIP/App`, `LWIP/Target`, and `TLS`.
Imported FreeRTOS, CMSIS, STM32 HAL, DP83848, lwIP, and Mbed TLS code keeps its
upstream style.

The conventions describe the target style. Existing names are changed only in
dedicated refactoring work, because renaming an API can affect several modules.

## General rules

- Use English names that describe purpose rather than implementation detail.
- Spell out words unless an abbreviation is established in the hardware or
  protocol documentation.
- Keep hardware and protocol names in their canonical form: `DS18B20`,
  `DP83848`, `Ethernet`, `HTTPS`, `IWDG`, `lwIP`, `Mbed TLS`, `NTP`, `RMII`,
  `RTC`, `SPI`, `TLS`, and `W25Q64`.
- Include units in names when the type alone does not make them clear, for
  example `periodMs`, `temperatureCentiDegrees`, or `humidityMilliPercent`.
- Avoid new identifiers beginning with an underscore. C reserves several such
  forms for the implementation.
- Use one term consistently for one concept. Prefer `Init`, `Read`, `Write`,
  `Measure`, `Get`, `Set`, `Lock`, `Unlock`, `Register`, and `Report`.

## Files and modules

- Use lowercase `snake_case` file names: `health_service.c`.
- Give a public header the same base name as its implementation file.
- STM32 initialization and board-level facilities belong in `Core`;
  FreeRTOS-based application services belong in `Srv`; project-owned lwIP
  integration belongs in `LWIP/App` or `LWIP/Target`; TLS configuration,
  trust data, and transport code belong in `TLS`.
- Keep imported source files in their upstream directory structure. Do not
  rename vendor files merely to satisfy this document.

## Functions

- Public functions use `PascalCase` with a module prefix:
  `HealthService_Init`, `Rtc_GetUnixTime`, `TlsTransport_Head`.
- Private functions use `camelCase` with a module prefix:
  `healthService_WatchdogReload`.
- Use an underscore between the module name and the operation for new public
  APIs.
- Use verbs for operations and nouns only for accessors that return an object.
- An `Init` function initializes hardware or creates a service and does not
  perform periodic work.
- A `Get` function does not transfer ownership of returned storage unless its
  documentation explicitly says otherwise.
- Boolean predicates should begin with `Is`, `Has`, or `Can`.

## Types and enumerators

- Public typedef names use `PascalCase` and end in `_TypeDef`:
  `SensorSnapshot_TypeDef`.
- Structure names describe one object; collection names describe the contained
  set.
- Enum constants and bit flags use uppercase `SNAKE_CASE` with a module prefix:
  `SENSOR_HEALTH_FAILED`.
- Structure members and function parameters use `camelCase`.
- New code should not introduce `_t` typedef names because POSIX reserves many
  names with that suffix. Existing Bosch calibration types can be migrated in
  a separate compatibility-aware refactor.

## Variables and constants

- Local variables and parameters use `camelCase`.
- File-local variables use `camelCase`; their `static` storage already conveys
  privacy.
- Compile-time constants and macros use uppercase `SNAKE_CASE`.
- Macros that behave like functions use uppercase `SNAKE_CASE` and parenthesize
  every parameter and the complete expression.
- FreeRTOS handles should identify the owned object, for example
  `healthTaskHandle`, `networkReadyEvent`, or `tlsMutex`.

## Documentation

Document every project-owned function where it is declared. Document a private
function immediately above its definition or prototype. Do not duplicate a
complete public API description in both the header and source file.

Function documentation uses this form:

```c
/**
  * @brief Perform an authenticated HTTPS HEAD request.
  * @param hostName (const char*) Non-null DNS host name used for SNI and
  *        certificate validation.
  * @param path (const char*) Non-null HTTP request path.
  * @param result (TlsTransport_ResultTypeDef*) Non-null result storage.
  * @retval (TlsTransport_StatusTypeDef) Transport completion status.
  */
```

Use `@param` only for real parameters; omit it for a `void` parameter list.
Use `@retval` only for functions that return a value. State units, ownership,
valid ranges, nullability, blocking behavior, and task/interrupt restrictions
when they matter.

Structure documentation lists the purpose and meaning of every member:

```c
/**
  * @brief Result of one HTTPS resource check.
  * @param statusCode (uint16_t) Parsed HTTP response status.
  * @param elapsedMs (uint32_t) Total request duration in milliseconds.
  * @param detail (int32_t) Layer-specific diagnostic value.
  */
```

## Compatibility notes

Project-owned adapters may expose conventional names around imported APIs, but
they must not rewrite vendor interfaces. STM32 HAL callbacks and handles,
FreeRTOS types, lwIP callbacks, Mbed TLS APIs, CMSIS symbols, and linker/startup
symbols retain their required upstream spelling.

Names tied to a peripheral register, protocol field, certificate property, or
datasheet formula should remain traceable to the corresponding specification.
Compatibility-sensitive renames belong in dedicated refactoring changes.
