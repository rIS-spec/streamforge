# StreamForge Benchmarking

## Method

`src/benchmark_client.cpp` opens N concurrent TCP connections (closed-loop:
each connection waits for a response before sending its next message),
sends `PRODUCE` requests of a fixed payload size, and measures wall-clock
round-trip latency per message plus aggregate throughput.

Run against `broker_server_epoll` with `--workers 4 --partitions 4` on
WSL2 Ubuntu 24.04. All numbers below are measured, not estimated.

## Results (100-byte payloads)

| Concurrency | Messages | Throughput (msg/s) | Throughput (MB/s) | p50 (ms) | p95 (ms) | p99 (ms) |
|---|---|---|---|---|---|---|
| 1   | 200  | 273.6 | 0.030 | 3.65   | 4.11   | 4.46   |
| 10  | 2000 | 737.5 | 0.082 | 13.63  | 14.96  | 17.29  |
| 50  | 5000 | 753.6 | 0.083 | 68.18  | 72.16  | 74.72  |
| 100 | 5000 | 723.4 | 0.080 | 139.23 | 142.66 | 144.43 |

## Bottleneck analysis

Throughput plateaus at ~730-750 msg/s from concurrency=10 onward and does
not increase further, while p50 latency grows roughly linearly with
concurrency. Flat throughput + linearly growing latency is the signature
of requests queuing behind a serialized resource, not genuine parallel
scaling.

Root cause: `Log::append()` (Phase 12) calls `fsync()` synchronously
before returning, and `Topic::publish()` holds a per-partition mutex for
the duration of `log_.append()`. With 4 partitions, at most 4 fsync
operations can be in flight concurrently — every message routed to the
same partition is fully serialized behind that partition's mutex+fsync.
4 partitions x ~185 msg/s per fsync-bound pipeline ≈ the measured ~740
msg/s ceiling.

This confirms durability (Phase 12's ACK-after-fsync policy), not the
network layer, the thread pool, or epoll, is the system's throughput
ceiling at this scale.

## Known future work (not implemented, listed honestly)

- Batched or async fsync (relaxes the ACK-after-fsync guarantee from
  Phase 12 — would need a new, explicitly documented durability trade-off,
  not a silent change)
- Larger partition counts to test whether the 4x-pipeline scaling model
  holds (untested above 4 partitions)
- Payload sizes above 100B (untested — current numbers should not be
  assumed to hold for larger messages)
