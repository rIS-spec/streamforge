# Storage

## On-disk format

Each partition has its own append-only log file (`<data_dir>/streamforge_p<N>.log`), containing a sequence of the exact same length-prefixed frames used on the wire (see [protocol.md](protocol.md)) — the log format and the network format are intentionally identical, so `readAll()` can reuse `decodePacket()` directly rather than maintaining a second parser.

```
[magic][length][payload][magic][length][payload]...
```

There is no separate index file, no offset table, and no checksum field. Reading the log means byte-scanning from the start, decoding one frame at a time.

## Durability: what `fsync()` actually guarantees

`Log::append()`:
```cpp
write(fd_, packet.data() + written, ...);   // copies bytes into OS page cache
fsync(fd_);                                  // blocks until page cache -> physical disk
```

The important distinction: a `write()` syscall returning success only means the kernel accepted the bytes into its page cache (still RAM). `fsync()` is what forces the kernel to flush those specific dirty pages to the actual storage device and blocks until that's confirmed. Without `fsync()`, a power loss between `write()` and the OS's own background writeback (which runs on its own schedule, not synchronously) would silently lose data that `append()` had already returned success for.

This is why `Log` is built on raw POSIX `open()`/`write()`/`fsync()`/`close()` rather than `std::ofstream`: `fsync()` requires a raw integer file descriptor, and `std::ofstream` deliberately does not expose one (it's a portable C++ abstraction; Windows doesn't have `fsync()` in this form at all).

## ACK policy: ACK-after-fsync

`Topic::publish()` calls `log_.append()` — which blocks until `fsync()` returns — **before** pushing to the in-memory queue and returning control to the caller. The broker's `PRODUCE` handler only sends the `OK` response after `Broker::publish()` (and therefore `Log::append()`, and therefore `fsync()`) has fully returned.

**Consequence, measured**: this is the direct cause of the ~3.6ms single-message latency floor and the ~730-750 msg/s throughput ceiling documented in [benchmarking.md](benchmarking.md) — every acknowledged message has synchronously round-tripped to physical disk before the client hears "OK." The alternative (ACK-before-fsync, or a batched/async fsync policy) would be faster but would mean "OK" no longer means "durably stored," which would need to be its own explicit, documented decision — not a silent default.

## Recovery: what actually happens on restart

`Topic`'s constructor calls `log_.readAll(log_path)` and pushes every returned message into the in-memory queue — this **is** the recovery mechanism; there is no separate "recovery mode" or startup validation pass beyond what `decodePacket`'s bounds-checking already provides for free (see [recovery.md](recovery.md) for the specific scenarios tested).

## Known limitations (stated, not hidden)

- **No consumer-offset tracking.** `consume()` only removes from the in-memory `std::queue`, never rewrites or truncates the log file. Because the log is replayed in full on every restart, a message that was already consumed before a restart will reappear in the queue after restart. Tests that construct multiple `Topic` instances against the same log path must `std::remove()` the file first to avoid leftover state from a previous run — this is a workaround for the test suite, not a fix for the underlying gap.
- **No checksums.** A record with a valid magic number and a valid (in-range) length, but corrupted payload bytes, will be silently accepted as valid data. Only a *truncated* record (not enough bytes for the declared length) is currently detected — see the `TruncatedTailRecordIsIgnoredNotCrashed` test in [recovery.md](recovery.md). Adding a CRC/checksum per record is listed as future work, not implemented.
