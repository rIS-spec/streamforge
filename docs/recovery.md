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

## Failure injection (Tier 4, `scripts/failure_injection.sh`)

Beyond the unit-level recovery tests above, three scenarios are tested against a real running `broker_server_epoll` subprocess over real TCP:

1. **`kill -9` mid-session.** Two messages produced, broker force-killed (not `SIGTERM` — an actual unclean death), restarted, both messages recovered in order. This is the strongest proof of the fsync-based durability claim in this project: it confirms survival through a death the process had zero chance to react to, not just a graceful shutdown.
2. **Corrupted on-disk log.** A valid message is written, the broker stops, then 37 random bytes are appended directly to the log file (simulating a crash mid-write of a second record). The broker starts cleanly against this corrupted file and correctly replays only the valid record — confirming `decodePacket`'s bounds/CRC checking (see [protocol.md](protocol.md)) protects startup, not just steady-state operation.
3. **Garbage flood against a live broker.** Five connections send junk bytes and disconnect; the broker remains fully responsive to real clients afterward (confirmed via `HEALTH`).

## What is *not* yet tested or handled

- **Bit-flip corruption that preserves a valid CRC** — cryptographically implausible but theoretically possible; not a realistic gap.
- **Multiple simultaneous `kill -9` restarts under concurrent load** — the tested scenario is a single client, single kill, clean restart. A broker killed while actively serving many concurrent producers has not been separately tested.