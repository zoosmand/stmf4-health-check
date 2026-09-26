# Factory-reset regression checks

Run these checks on target hardware after changing the factory-reset, buzzer,
watchdog, or W25Q64 code. Capture the RS485 diagnostic output and verify that
the default watchdog task remains healthy throughout each non-resetting case.

## Button timing

1. Hold S1 for less than 10 seconds and release it. Verify that no warning is
   played and persistent configuration is unchanged.
2. Hold S1 for 10 seconds, release it, and do not click again. Verify that the
   reset starts only after the complete 10-second cancellation window.
3. During the cancellation window, try one click, two clicks more than 600 ms
   apart, and two clicks less than 600 ms apart. Only the final case may cancel
   the reset, and it must play the three-tone acknowledgement.
4. Repeat the valid double click at the beginning and end of the cancellation
   window to exercise both timing boundaries.

## Flash exclusion and failures

1. Start a persistent configuration update and hold S1 concurrently. Verify
   that the reset waits for the current flash transaction, then prevents any
   other flash transaction from interleaving with marker creation and erasure.
2. Inject erase, program, read, and erased-data verification failures. Verify
   that the recovery marker is not cleared after any reset-owned sector fails.
3. Remove power after marker verification and after each of the 15 data-sector
   erases. On every reboot, verify that recovery erases and verifies all reset
   sectors before any persistent store is opened.

## Buzzer concurrency

1. Queue a health-check alert immediately before each synchronous reset tone
   sequence. Verify that warning and cancellation callers do not return before
   their own sequence completes.
2. Inject a buzzer completion timeout and then issue another synchronous tone
   request. Verify that the delayed completion cannot satisfy the new request.

## Completion criteria

After a completed or power-loss-recovered reset, confirm that users,
health-check configuration and logs, trust anchors, and uploaded TLS server
credentials are gone, while compiled factory defaults are recreated normally.
