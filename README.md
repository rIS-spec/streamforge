# StreamForge

A Kafka-inspired distributed message broker built from scratch in C++20 — raw POSIX sockets, epoll-based event loop, append-only durable logs, and hash/round-robin partitioning. No message-queue libraries, no networking frameworks: every layer from the TCP accept loop to the on-disk binary format is hand-built to understand the actual engineering trade-offs a production system like Kafka has to make.

[![CI](https://github.com/rIS-spec/streamforge/actions/workflows/ci.yml/badge.svg)](https://github.com/rIS-spec/streamforge/actions/workflows/ci.yml)

---

## Why this project exists

Most student systems projects stop at "it compiles and a demo works." StreamForge is built against a stricter bar: **every claim is either measured or explicitly marked as not yet proven.** No number in this README was estimated — every throughput, latency, and test count below came from actually running the code and pasting the terminal output. Where a limitation exists (no consumer-offset tracking, no checksums yet), it's stated plainly rather than glossed over.

## Architecture

```
                     ┌─────────────────────────────┐
                     │   broker_server_epoll        │
                     │   (single epoll_wait loop)   │
                     └──────────────┬───────────────┘
                                    │ EPOLLONESHOT-dispatched
                                    ▼
                     ┌─────────────────────────────┐
                     │       ThreadPool (N)          │
                     │  decode → route → respond     │
                     └──────────────┬───────────────┘
                                    ▼
                     ┌─────────────────────────────┐
                     │           Broker              │
                     │  Partitioner: hash(key) % N   │
                     │  or round-robin (unkeyed)     │
                     └──────────────┬───────────────┘
                        ┌───────────┼───────────┐
                        ▼           ▼           ▼
                   ┌─────────┐ ┌─────────┐ ┌─────────┐
                   │ Topic 0 │ │ Topic 1 │ │ Topic N │   each: in-memory
                   │ +Log 0  │ │ +Log 1  │ │ +Log N  │   queue + append-only
                   └─────────┘ └─────────┘ └─────────┘   fsync'd binary log
```

**Wire protocol:** length-prefixed binary framing — `[4-byte magic "SFRG"][4-byte length][payload]`, network byte order, 10MB payload cap enforced before any allocation.

**Concurrency model:** single-threaded epoll accept/dispatch loop hands ready sockets to a fixed thread pool via `EPOLLONESHOT` (re-armed explicitly after each worker finishes) — decouples open-connection count from thread count, and closed a real double-`recv()` race bug caught during development (see [Engineering decisions](#engineering-decisions-and-trade-offs)).

**Durability model:** every `Log::append()` performs a real `fsync()` syscall (via raw `open()`/`write()`/`fsync()`, not `std::ofstream`, which cannot expose a file descriptor to `fsync()`) before returning — this is an explicit **ACK-after-fsync** policy: the broker does not tell a producer "received" until the message is confirmed on physical disk, not just in the OS page cache.

## What's implemented

| Phase | Feature | Status |
|---|---|---|
| 0–3 | Toolchain, `Message`/`Topic`/`ThreadPool` core | ✅ |
| 4–5 | Raw POSIX socket server, length-prefixed binary protocol | ✅ |
| 6 | Append-only log, crash-recovery replay on startup | ✅ |
| 7 | Hash/round-robin partitioning, multi-partition `Broker` | ✅ |
| 8 | epoll-based event loop, `EPOLLONESHOT` race fix | ✅ |
| 9 | CLI configuration (`--port`, `--workers`, `--partitions`, `--data-dir`) | ✅ |
| 10 | Oversized-payload rejection, syscall error handling | ✅ |
| 11 | Graceful shutdown on `SIGINT`/`SIGTERM` | ✅ |
| 12 | fsync-based durability, explicit ACK-after-fsync policy | ✅ |
| 14 | Recovery hardening — normal restart, no-clean-close, truncated tail | ✅ |
| 20 | Real measured benchmarking (throughput + p50/p95/p99 latency) | ✅ |
| 23 | Negative-path test coverage (bad magic, oversized, invalid partition) | ✅ |
| 27 | Docker — multi-stage build, non-root user, persistent volume | ✅ |
| 28 | CI — build+test, ASan/UBSan, clang-tidy (GitHub Actions) | ✅ |
| 30–31 | Documentation, ADRs | 🚧 in progress |
| — | Checksums, consumer-offset tracking, multi-broker replication | Explicitly deferred — see [Known limitations](#known-limitations-stated-honestly) |

## Test results (measured, not claimed)

**13 GoogleTest cases, 100% passing**, run with `ctest --output-on-failure`:

```
      Start  1: MessageTest.FieldsStoreCorrectly ......................   Passed    0.01 sec
      Start  2: TopicTest.ConsumesInFIFOOrder .........................   Passed    0.02 sec
      Start  3: TopicTest.ConsumeOnEmptyQueueReturnsFalse .............   Passed    0.01 sec
      Start  4: TopicConcurrencyTest.HandlesManyProducersSafely .......   Passed   31.20 sec
      Start  5: ThreadPoolTest.ProcessesAllSubmittedTasks .............   Passed    3.08 sec
      Start  6: ProtocolTest.RoundTripSimplePayload ...................   Passed    0.01 sec
      Start  7: ProtocolTest.IncompleteDataIsRejected .................   Passed    0.01 sec
      Start  8: ProtocolTest.OversizedLengthIsRejected ................   Passed    0.01 sec
      Start  9: ProtocolTest.BadMagicNumberIsRejected .................   Passed    0.01 sec
      Start 10: ProtocolTest.EmptyDataIsRejected ......................   Passed    0.01 sec
      Start 11: LogTest.AppendAndReadBack .............................   Passed    0.02 sec
      Start 12: BrokerTest.SameKeyAlwaysGoesToSamePartition ...........   Passed    0.02 sec
      Start 13: BrokerTest.ConsumeOnInvalidPartitionReturnsFalse ......   Passed    0.01 sec
      Start 14: RecoveryTest.NormalRestartPreservesMessages ...........   Passed    0.02 sec
      Start 15: RecoveryTest.DataSurvivesWithoutCleanClose ............   Passed    0.01 sec
      Start 16: RecoveryTest.TruncatedTailRecordIsIgnoredNotCrashed ...   Passed    0.01 sec

100% tests passed, 0 tests failed out of 16
```

**Notably:** `TopicConcurrencyTest.HandlesManyProducersSafely` drives **10 threads × 1,000 messages = 10,000 concurrent publishes** against a single mutex-protected `Topic`, verified zero message loss.

The same suite also runs clean under **AddressSanitizer + UndefinedBehaviorSanitizer** in CI (see [`.github/workflows/ci.yml`](.github/workflows/ci.yml)) — no use-after-free, buffer overrun, or undefined behavior detected across the epoll server, the raw-fd `Log`, the thread pool, or the manual byte-level protocol decoder.

## Benchmark results (measured, 100-byte payloads)

Measured with `benchmark_client` against `broker_server_epoll --workers 4 --partitions 4` on WSL2 Ubuntu 24.04. Full method and analysis in [`docs/benchmarking.md`](docs/benchmarking.md).

| Concurrency | Messages | Throughput (msg/s) | p50 latency | p95 latency | p99 latency |
|---|---|---|---|---|---|
| 1   | 200  | 273.6 | 3.65 ms   | 4.11 ms   | 4.46 ms   |
| 10  | 2000 | 737.5 | 13.63 ms  | 14.96 ms  | 17.29 ms  |
| 50  | 5000 | 753.6 | 68.18 ms  | 72.16 ms  | 74.72 ms  |
| 100 | 5000 | 723.4 | 139.23 ms | 142.66 ms | 144.43 ms |

**What the data shows:** throughput plateaus at ~730–750 msg/s from concurrency=10 onward while latency grows linearly — the signature of requests queuing behind a serialized resource. Root cause, confirmed by design: `fsync()` is called synchronously inside each partition's locked `append()`, so 4 partitions give exactly 4 fsync-bound pipelines (4 × ~185 msg/s ≈ the measured ceiling). This is the direct, measured cost of the Phase 12 ACK-after-fsync durability guarantee — not a network or threading bottleneck. Full analysis in the linked doc.

## Getting started

### Build natively

```bash
git clone https://github.com/rIS-spec/streamforge.git
cd streamforge
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Run the test suite

```bash
cd build
ctest --output-on-failure
```

### Run the broker

```bash
./build/broker_server_epoll --port 9001 --workers 4 --partitions 4 --data-dir ./data
```

### Run with Docker

```bash
docker compose up -d
docker compose logs -f
```

Logs persist across `docker compose restart` via a named volume — verified live, including confirming `SIGTERM` is received and handled gracefully by the containerized process (not force-killed), since the entrypoint uses exec-form (`exec stdbuf -oL -eL /app/broker_server_epoll ...`) so the binary runs as PID 1.

### Run the benchmark client

```bash
./build/benchmark_client --concurrency 10 --messages 200 --payload-size 100
```

## Engineering decisions and trade-offs

A few decisions worth explaining, not just stating:

- **Why raw POSIX sockets/fd's instead of a library.** The entire point of this project is systems-programming depth — using Boost.Asio or similar would answer "does it work" but not "do I understand why it works."
- **Why `std::ofstream` was replaced with raw `open()`/`write()`/`fsync()` in `Log`.** `fsync()` is a POSIX syscall requiring an integer file descriptor; `std::ofstream` deliberately hides that fd as part of being a portable C++ abstraction. There is no standard way to `fsync()` an `ofstream`. Durability could not be honestly implemented without dropping to the raw syscall layer.
- **The `EPOLLONESHOT` bug, found and fixed during development.** Level-triggered epoll kept reporting the same ready client socket on every `epoll_wait()` call even while a worker was already processing it — two workers could `recv()` the same connection concurrently and corrupt the byte stream. Symptom was silent: `test_client` produce/consume returned nothing, no crash. Fixed with `EPOLLONESHOT` plus an explicit re-arm after each worker finishes. This is the kind of bug that's easy to introduce and hard to see without understanding epoll's edge-triggered vs. level-triggered semantics.
- **ACK-after-fsync, chosen explicitly over ACK-before-fsync.** Slower (proven — see benchmark table above), but what the broker tells a producer is true. The alternative (async/batched fsync) is legitimate future work, not implemented here, because it reopens a durability-window trade-off that deserves its own explicit decision, not a silent optimization.

## Known limitations (stated honestly)

- **No consumer-offset tracking.** `Topic`'s constructor replays the *entire* log file on every restart; `consume()` only removes from the in-memory queue, never rewrites the log. A "consumed" message will replay again after a restart. This is a known, documented gap — not yet fixed.
- **No checksums on log records.** `readAll()` can detect a *truncated* tail record (not enough bytes for a full packet) but not a *bit-flipped* one (right length, corrupted content) — there is no CRC/checksum in the on-disk format yet.
- **No integration test drives the live server binary end-to-end over a real TCP connection in CI** — current CI coverage is unit/component-level (`test_broker`, `test_recovery`, etc.), not a full socket-level integration test. Tracked as a gap, not silently omitted.
- **Single-node only.** No replication, no leader election, no consumer groups. Explicitly out of scope for this project (a possible future v2).

## Environment

Developed and tested on WSL2 Ubuntu 24.04 LTS, g++ 13.3.0, CMake 3.28.3, GoogleTest v1.14.0. CI runs on `ubuntu-24.04` GitHub-hosted runners.

## License

MIT
### Sanitizer builds (ASan+UBSan, TSan)

```bash
cmake --preset asan && cmake --build --preset asan && (cd build-san && ctest --output-on-failure)
cmake --preset tsan && cmake --build --preset tsan && (cd build-tsan && ctest --output-on-failure)
```

**WSL2 note**: GCC's ThreadSanitizer runtime segfaults on WSL2's default ASLR entropy ("unexpected memory mapping" or a bare segfault with no report). Fix per session (resets on WSL restart):
```bash
sudo sysctl -w vm.mmap_rnd_bits=28
```

### Sanitizer builds (ASan+UBSan, TSan)

```bash
cmake --preset asan && cmake --build --preset asan && (cd build-san && ctest --output-on-failure)
cmake --preset tsan && cmake --build --preset tsan && (cd build-tsan && ctest --output-on-failure)
```

**WSL2 note**: GCC's ThreadSanitizer runtime segfaults on WSL2's default ASLR entropy ("unexpected memory mapping" or a bare segfault with no report). Fix per session (resets on WSL restart):
```bash
sudo sysctl -w vm.mmap_rnd_bits=28
```
