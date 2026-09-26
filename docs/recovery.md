# Recovery

## The recovery mechanism

There is no separate "recovery module." `Topic`'s constructor calls `Log::readAll(log_path)` and pushes every successfully-decoded message into the in-memory queue. This single code path handles every startup, whether the previous process exited cleanly or was killed — because durability comes from `fsync()` happening synchronously on every `append()` (see [storage.md](storage.md)), not from any special shutdown procedure.

## Three scenarios, tested (`tests/test_recovery.cpp`)

### 1. Normal restart (`RecoveryTest.NormalRestartPreservesMessages`)
Publish two messages, let the `Topic`/`Log` destructors run (simulating a clean process exit), construct a fresh `Topic` against the same log path, and confirm both messages replay in the original order via `consume()`.

### 2. No clean close (`RecoveryTest.DataSurvivesWithoutCleanClose`)
Publish one message through `Log::append()` directly, without relying on any additional flush/close step beyond what `append()` itself already does. Construct a fresh `Log`, call `readAll()`, and confirm the message is present.

This test's real point: **`fsync()`, not RAII/destructor cleanup, is what makes data durable here.** `append()`'s `fsync()` call means the data is already safe on disk by the time `append()` returns — a graceful `close()` afterward is good practice but not what's actually protecting the data. This was chosen as the most honest way to approximate "the process was killed" in a unit test, since a test process can't literally `SIGKILL` itself and continue running assertions afterward.

### 3. Truncated/corrupt tail record (`RecoveryTest.TruncatedTailRecordIsIgnoredNotCrashed`)
Write one complete, valid message via `Log`. Then, bypassing `Log` entirely, append 5 raw bytes directly to the file that look like the start of a magic number but never complete a full frame — simulating what a process crash mid-`write()` on a second message would leave behind. Construct a fresh `Log`, call `readAll()`, and confirm exactly one message is returned (the complete one) — no crash, no exception, no fabricated second message.

This works because `decodePacket()` already bounds-checks before trusting any field (see [protocol.md](protocol.md)) — `readAll()`'s loop simply stops the moment `decoded.valid` is `false`, which happens naturally at a truncated tail. No additional "corruption detection" code was needed for this specific case.

## What is *not* yet tested or handled

- **Bit-flip corruption** — a record with a *valid* magic number and *valid* (in-range) length, but corrupted payload bytes, is currently indistinguishable from legitimate data. There's no checksum to catch this (see [storage.md](storage.md), Known limitations).
- **A literal `kill -9` of the live server process** — the recovery tests above approximate this at the `Log` unit level, but there is no integration test that starts `broker_server_epoll` as a real subprocess, force-kills it mid-write, and verifies the next startup over a real TCP connection. Your original handoff notes mention this was verified manually once (2 messages produced, `Ctrl+C`, restart, both consumed correctly) — that was a real, live verification, but it is not automated or repeatable as a test today.
