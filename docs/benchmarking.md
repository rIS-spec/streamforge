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

## Syscall-level confirmation (strace -c, Tier 4)

The throughput analysis above was based on code-level reasoning about
where fsync() sits in the critical path. This was independently
confirmed with `strace -f -c` against a live `--concurrency 10`
benchmark run (2000 messages):

```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 54.09    3.962128        1981      2000           fsync
 22.40    1.641048        1149      1427         1 epoll_wait
  6.55    0.479834         119      4010      2000 recvfrom
  6.38    0.466957         233      2000           sendto
  4.61    0.337846         167      2021           epoll_ctl
  3.84    0.281212         139      2012           write
  1.84    0.134964         203       663       203 futex
------ ----------- ----------- --------- --------- ----------------
100.00    7.324650         511     14329      2207 total
```

fsync accounts for 54% of all syscall time across every thread — more
than double epoll_wait (22.4%, mostly legitimate idle-waiting, not
cost) and over 14x the plain write syscall (3.84%) that copies bytes
into the page cache. This directly confirms the write itself is
cheap; the durability guarantee (forcing those bytes to physical disk
before acknowledging) is what's expensive — exactly the trade-off
documented in [ADR 007](adrs/adr-007-ack-after-fsync.md).

2000 fsync calls exactly matches 2000 completed messages (one fsync
per Log::append(), as designed). The errors count on recvfrom (2000)
and futex (203) are expected EAGAIN/EWOULDBLOCK results from
non-blocking epoll-driven sockets (Phase 8), not real errors.

## Known future work (not implemented, listed honestly)

- Batched or async fsync (relaxes the ACK-after-fsync guarantee from
  Phase 12 — would need a new, explicitly documented durability trade-off,
  not a silent change)
- Larger partition counts to test whether the 4x-pipeline scaling model
  holds (untested above 4 partitions)
- Payload sizes above 100B (untested — current numbers should not be
  assumed to hold for larger messages)
