# Resource certificate-expiry regression checks

Run these checks on target hardware with an NTP-synchronized RTC and HTTPS test
servers whose leaf certificates have controlled expiration dates.

Before configuring test certificates, call `POST /api/v1/buzzer/test` with
`{"pattern":"certificate_expiry","resource_index":0}` (then indices `1` and
`2`) and confirm the three identification patterns sound as expected.

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
   valid, nearly expired certificate. Confirm the expiry melody and the normal
   failed-health-check alert are both played.
5. Replace a resource's nearly expired certificate with a different nearly
   expired certificate. Confirm the changed expiration time starts a new
   warning interval.
6. Cause DNS, TCP, and TLS-handshake failures where no verified peer certificate
   is available. Confirm those failures do not produce an expiry melody.
7. Exercise simultaneous warnings for all three resources and a factory-reset
   warning/cancellation. Confirm pending requests are serialized and none of
   the distinct warning patterns is lost.
8. Power-cycle within an hourly interval. Confirm one early repeat warning is
   acceptable because warning timestamps intentionally are not persisted.

Monitor `uxTaskGetStackHighWaterMark()` for the health-check and buzzer tasks
during these checks before reducing either static stack allocation.
