# Outbound callback regression checks

Run these checks on target hardware with a trusted HTTPS capture endpoint.
Configure the device through the management endpoints documented in the main
[README](../../README.md), and use a dedicated trust anchor appropriate for the
capture endpoint.

1. Verify that a factory-fresh device reports the default callback target but
   keeps callback delivery disabled.
2. Configure GET and POST in turn through `PUT /api/v1/callback/config` and
   confirm that each completed health check produces exactly `resource`,
   `status`, `http_status`, and `elapsed_ms`.
3. Verify POST uses `application/json` and an exact `Content-Length`; verify GET
   preserves an existing query string and appends the four parameters with `&`.
4. Try invalid methods, ports, trust-anchor IDs, hosts, paths, and oversized
   values. Each must be rejected without changing the last valid snapshot.
5. Power-cycle during each half of an A/B configuration update and confirm that
   the newest completely verified snapshot is selected on boot.
6. Delay callback responses until more than three results are pending. Confirm
   that health checks continue and the oldest queued result is dropped.
7. Keep a management TLS connection active while a callback is pending. Confirm
   that TLS operations serialize and delivery resumes after the lock is free.
8. Remove network connectivity and restore it. Confirm bounded timeout behavior
   and that the callback task remains operational for later results.
9. Attempt to delete the callback's active trust anchor. Confirm the API returns
   `409 trust_anchor_in_use` while the callback is enabled.
10. Perform a factory reset and confirm both callback A/B sectors are erased and
    the callback returns to its disabled default configuration.

Monitor `uxTaskGetStackHighWaterMark()` during GET, POST, DNS failure, handshake
failure, and successful TLS 1.3 delivery before reducing the callback task's
static stack allocation.
