# Resource certificate-expiry regression checks

Run these checks on target hardware with an NTP-synchronized RTC and HTTPS test
servers whose leaf certificates have controlled expiration dates.

Before configuring test certificates, call `POST /api/v1/buzzer/test` with
`{"pattern":"certificate_expiry","resource_index":0}` (then indices `1` and
`2`) and confirm the three identification patterns sound as expected.
Repeat with `pattern` set to `health_check`; confirm Morse SOS is followed by
the same one-, two-, or three-beep resource identifier.

1. Configure resource slots `0`, `1`, and `2` with valid certificates expiring
   more than ten days in the future. Confirm normal checks produce no expiry
   melody.
2. Configure each slot with a valid certificate expiring within ten days.
   Confirm each produces a rising three-note melody followed by one, two, or
   three identification beeps respectively.
3. Continue checks for more than one hour. Confirm each affected resource warns
   no more than once in any hour and warns again on the first check after its
   hourly interval elapses.
4. Configure a resource that returns a non-200 HTTP status with an otherwise
   valid, nearly expired certificate. Confirm only the failed-health-check SOS
   is played; failed resources do not receive a separate expiry warning.
5. Replace a resource's nearly expired certificate with a different nearly
   expired certificate. Confirm the changed expiration time starts a new
   warning interval.
6. Cause DNS, TCP, and TLS-handshake failures where no verified peer certificate
   is available. Confirm those failures do not produce an expiry melody.
7. Fail two or three resources in one check cycle. Confirm one SOS is followed
   after one second by each affected resource identifier in ascending order,
   with a one-second separator between identifier groups.
8. In one cycle, fail at least one resource while a different, healthy resource
   has an expiring certificate. Confirm both alert signatures begin within
   three seconds, followed by the failed-resource identifiers and then the
   certificate-expiry identifiers. Also exercise a factory-reset warning and
   cancellation; confirm pending alert classes are serialized and not lost.
9. Power-cycle within an hourly interval. Confirm one early repeat warning is
   acceptable because warning timestamps intentionally are not persisted.
10. Read `GET /api/v1/health-check/logs`. For verified certificates, confirm the
   ISO UTC expiry, rounded-up `days_left`, anchor ID, and ten-day `expiring`
   flag. For pre-handshake failures, confirm both date fields are `null`.

Monitor `uxTaskGetStackHighWaterMark()` for the health-check and buzzer tasks
during these checks before reducing either static stack allocation.

---

&copy; 2017-2026 Askug Ltd., Dmitry Slobodchikov
