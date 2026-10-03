# StreamForge

A Kafka-inspired distributed message broker built from scratch in C++20 — raw POSIX sockets, epoll-based event loop, append-only durable logs, and hash/round-robin partitioning. No message-queue libraries, no networking frameworks: every layer from the TCP accept loop to the on-disk binary format is hand-built to understand the actual engineering trade-offs a production system like Kafka has to make.

[![CI](https://github.com/rIS-spec/streamforge/actions/workflows/ci.yml/badge.svg)](https://github.com/rIS-spec/streamforge/actions/workflows/ci.yml)

---

## Why this project exists

Most student systems projects stop at "it compiles and a demo works." StreamForge is built against a stricter bar: **every claim is either measured or explicitly marked as not yet proven.** No number in this README was estimated — every throughput, latency, and test count below came from actually running the code and pasting the terminal output. Where a limitation exists, it's stated plainly rather than glossed over. See [docs/demo.md](docs/demo.md) for a full real transcript, including a CI failure that was caught and fixed on camera, not edited out.

## For non-technical readers

Imagine a busy post office. Trucks (producers) drop off packages all day. People (consumers) come to pick packages up, sometimes hours or days later. The post office's job is to:

1. **Never lose a package**, even if the power goes out mid-shift.
2. **Keep multiple counters open** so one slow package doesn't make everyone else wait.
3. **Sort packages into the right bin** (partition) so related ones end up together.
4. **Survive a crash.** If the building loses power *right as* a package is being filed, the next morning's reopening must not lose that package — and must not hand out a package that was already mailed out.

StreamForge is that post office, built in software. "Producers" are programs that send messages (packages); "consumers" are programs that read them later. Instead of a building, StreamForge is a single program (the "broker") that listens on the network, accepts connections from many producers and consumers at once, and writes every message safely to disk before telling the sender "received" — so a message is never acknowledged as safe unless it actually is.

The project doesn't just claim this works — it proves it. One of the tests literally kills the broker's process instantly, mid-operation (the software equivalent of yanking the power cord), restarts it, and checks that nothing was lost. That test passes, every time, and is run automatically every time the code changes (see [CI status](#test-results-measured-not-claimed) below).

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

**Wire protocol:** length-prefixed, versioned binary framing — `[4-byte magic "SFRG"][1-byte version][4-byte length][payload][4-byte CRC32]`, network byte order, 10MB payload cap enforced before any allocation, checksum verified on every decode.

**Concurrency model:** single-threaded epoll accept/dispatch loop hands ready sockets to a fixed thread pool via `EPOLLONESHOT` (re-armed explicitly after each worker finishes) — decouples open-connection count from thread count, and closed a real double-`recv()` race bug caught during development (see [Engineering decisions](#engineering-decisions-and-trade-offs)).

**Durability model:** every `Log::append()` performs a real `fsync()` syscall (via raw `open()`/`write()`/`fsync()`, not `std::ofstream`, which cannot expose a file descriptor to `fsync()`) before returning — this is an explicit **ACK-after-fsync** policy: the broker does not tell a producer "received" until the message is confirmed on physical disk, not just in the OS page cache. This has been verified to survive a literal `kill -9` (see [docs/demo.md](docs/demo.md)), not just a graceful shutdown.

## What's implemented

| Area | Feature | Status |
|---|---|---|
| Core | `Message`/`Topic`/`ThreadPool`, raw POSIX socket server | ✅ |
| Wire protocol | Length-prefixed binary framing, protocol versioning, CRC32 checksums | ✅ |
| Storage | Append-only fsync'd log, crash-recovery replay on startup | ✅ |
| Routing | Hash/round-robin partitioning, multi-partition `Broker` | ✅ |
| Networking | epoll event loop, `EPOLLONESHOT` race fix, CLI config | ✅ |
| Reliability | Graceful shutdown, oversized-payload rejection, syscall error handling | ✅ |
| Durability | fsync-based ACK-after-fsync policy, 3-scenario recovery hardening | ✅ |
| Tooling | `sf-client` interactive CLI, `benchmark_client`, structured JSON logging | ✅ |
| Observability | `HEALTH`/`METRICS` commands, `strace`-based bottleneck analysis | ✅ |
| Security | Per-client buffer cap, `--max-connections`, idle-connection timeout | ✅ |
| Testing | 18 GoogleTest cases, negative-path tests, failure injection (`kill -9`, log corruption) | ✅ |
| Quality gates | ASan/UBSan/TSan in CI, `-Wall -Wextra -Wpedantic` (zero warnings), clang-format, clang-tidy | ✅ |
| Deployment | Docker (multi-stage, non-root, persistent volume), CMake presets (Debug/Release/ASan/TSan) | ✅ |
| CI/CD | GitHub Actions: build+test+smoke test across plain/ASan/TSan builds, lint | ✅ |
| Docs | README, 5 architecture docs, 7 ADRs, benchmarking, real end-to-end demo transcript | ✅ |
| — | Consumer-offset tracking, multi-broker replication, HTTP `/health` (vs. protocol command) | Explicitly deferred — see [Known limitations](#known-limitations-stated-honestly) |

## Test results (measured, not claimed)

**18 GoogleTest cases, 100% passing**, run with `ctest --output-on-failure`, including CRC32 corruption detection, protocol version mismatch rejection, and invalid-partition handling:

```
100% tests passed, 0 tests failed out of 18
```

**Notably:** `TopicConcurrencyTest.HandlesManyProducersSafely` drives **10 threads × 1,000 messages = 10,000 concurrent publishes** against a single mutex-protected `Topic`, verified zero message loss.

The same suite runs clean under **AddressSanitizer + UndefinedBehaviorSanitizer** and **ThreadSanitizer** in CI — no memory errors, undefined behavior, or data races detected. CI also runs a **server-level smoke test** (`scripts/smoke_test.sh`) against the actual `broker_server_epoll` binary over real TCP under all three build variants — this is what caught a real regression mid-project (see [docs/demo.md](docs/demo.md) for the full story of that bug, found and fixed, not hidden).

## Benchmark results (measured, 100-byte payloads)

Measured with `benchmark_client` against `broker_server_epoll --workers 4 --partitions 4` on WSL2 Ubuntu 24.04. Full method and analysis in [`docs/benchmarking.md`](docs/benchmarking.md).

| Concurrency | Messages | Throughput (msg/s) | p50 latency | p95 latency | p99 latency |
|---|---|---|---|---|---|
| 1   | 200  | 273.6 | 3.65 ms   | 4.11 ms   | 4.46 ms   |
| 10  | 2000 | 737.5 | 13.63 ms  | 14.96 ms  | 17.29 ms  |
| 50  | 5000 | 753.6 | 68.18 ms  | 72.16 ms  | 74.72 ms  |
| 100 | 5000 | 723.4 | 139.23 ms | 142.66 ms | 144.43 ms |

**What the data shows:** throughput plateaus at ~730–750 msg/s from concurrency=10 onward while latency grows linearly — the signature of requests queuing behind a serialized resource. Root cause, confirmed both by design and by `strace -c` syscall analysis: `fsync()` accounts for **54% of all syscall time** (see [docs/benchmarking.md](docs/benchmarking.md#syscall-level-confirmation-strace--c-tier-4)) — this is the direct, measured cost of the ACK-after-fsync durability guarantee, not a network or threading bottleneck.

## Getting started

### Build natively

```bash
git clone https://github.com/rIS-spec/streamforge.git
cd streamforge
cmake --preset release
cmake --build --preset release
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

### Talk to it interactively

```bash
./build/sf_client --port 9001
```
```
sf> PRODUCE hello world
OK
sf> CONSUME 0
MSG: hello world
sf> HEALTH
HEALTHY
```

### Run with Docker

```bash
docker compose up -d
docker compose logs -f
```

Logs persist across `docker compose restart` via a named volume — verified live, including confirming `SIGTERM` is received and handled gracefully by the containerized process (not force-killed), since the entrypoint uses exec-form so the binary runs as PID 1.

### Run the benchmark client

```bash
./build/benchmark_client --port 9001 --concurrency 10 --messages 200 --payload-size 100
```

### Run the full end-to-end demo

```bash
bash scripts/demo.sh
```

Produces, hard-kills the broker mid-session, restarts it, confirms recovery, benchmarks, and checks CI — see [docs/demo.md](docs/demo.md) for a full annotated transcript of a real run.

### Sanitizer builds (ASan+UBSan, TSan)

```bash
cmake --preset asan && cmake --build --preset asan && (cd build-san && ctest --output-on-failure)
cmake --preset tsan && cmake --build --preset tsan && (cd build-tsan && ctest --output-on-failure)
```

**WSL2 note**: GCC's ThreadSanitizer runtime segfaults on WSL2's default ASLR entropy ("unexpected memory mapping" or a bare segfault with no report). Fix per session (resets on WSL restart):
```bash
sudo sysctl -w vm.mmap_rnd_bits=28
```

## Engineering decisions and trade-offs

A few decisions worth explaining, not just stating — full reasoning for each in [`docs/adrs/`](docs/adrs/):

- **Why raw POSIX sockets/fd's instead of a library.** The entire point of this project is systems-programming depth — using Boost.Asio or similar would answer "does it work" but not "do I understand why it works."
- **Why `std::ofstream` was replaced with raw `open()`/`write()`/`fsync()` in `Log`.** `fsync()` is a POSIX syscall requiring an integer file descriptor; `std::ofstream` deliberately hides that fd as part of being a portable C++ abstraction. There is no standard way to `fsync()` an `ofstream`. Durability could not be honestly implemented without dropping to the raw syscall layer.
- **The `EPOLLONESHOT` bug, found and fixed during development.** Level-triggered epoll kept reporting the same ready client socket on every `epoll_wait()` call even while a worker was already processing it — two workers could `recv()` the same connection concurrently and corrupt the byte stream. Symptom was silent: produce/consume returned nothing, no crash. Fixed with `EPOLLONESHOT` plus an explicit re-arm after each worker finishes.
- **ACK-after-fsync, chosen explicitly over ACK-before-fsync.** Slower (proven — see benchmark table above), but what the broker tells a producer is true. See [ADR 007](docs/adrs/adr-007-ack-after-fsync.md) for the full trade-off.
- **A real regression, caught by CI, fixed in the open.** Mid-project, a hand-applied patch silently broke the server's ability to answer any request at all — the unit tests (which never start the server binary) didn't catch it, but the server-level smoke test added specifically to close that gap did. Full story in [docs/demo.md](docs/demo.md).

## Known limitations (stated honestly)

- **No consumer-offset tracking.** `Topic`'s constructor replays the *entire* log file on every restart; `consume()` only removes from the in-memory queue, never rewrites the log. A "consumed" message will replay again after a restart.
- **In-memory metrics reset on restart.** `METRICS`' `messages_produced`/`messages_consumed` counters are `std::atomic` in-process counters, not persisted — unlike the message log itself, which is fully durable. Documented live in [docs/demo.md](docs/demo.md) rather than hidden.
- **`HEALTH`/`METRICS` are protocol commands, not literal HTTP endpoints.** A true `/health` endpoint would mean a second listener speaking HTTP on a separate port — deliberately not built, so the project has one protocol built deeply rather than two built shallowly.
- **No integration test drives the live server binary over a real TCP connection from inside GoogleTest** — that coverage exists, but as a separate shell-based smoke test (`scripts/smoke_test.sh`), not a `ctest` case.
- **Single-node only.** No replication, no leader election, no consumer groups. Explicitly out of scope for this project (a possible future v2).

## Environment

Developed and tested on WSL2 Ubuntu 24.04 LTS, g++ 13.3.0, CMake 3.28.3, GoogleTest v1.14.0. CI runs on `ubuntu-24.04` GitHub-hosted runners.

## License

MIT