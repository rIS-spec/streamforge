# ADR 007: ACK-after-fsync, not ACK-before-fsync

## Context
`Log::append()` performs a real `fsync()` syscall before returning. The broker's `PRODUCE` response can be sent either before that `fsync()` completes (ACK-before-fsync — faster, but "OK" might be a lie if the process crashes in the gap) or after it completes (ACK-after-fsync — slower, but "OK" is always true).

## Decision
ACK-after-fsync. `Broker::publish()` (and therefore `Log::append()`, and therefore `fsync()`) fully completes before the `PRODUCE` handler in `broker_server_epoll.cpp` sends its response.

## Alternatives considered
- **ACK-before-fsync** — the response is sent as soon as the write is queued (or even before `write()` is called at all), with `fsync()` happening asynchronously afterward. Meaningfully faster (no round-trip to physical disk on the client-facing critical path), but introduces a real window where an acknowledged message can be lost to a crash or power failure — data loss the producer was explicitly told didn't happen.
- **Batched/grouped fsync** — accumulate several pending writes and `fsync()` once for the batch, trading a small amount of added worst-case latency for meaningfully higher throughput. A legitimate middle ground, not implemented here.

## Trade-offs
The measured cost of ACK-after-fsync is documented directly in `docs/benchmarking.md`: a ~3.6ms latency floor even at concurrency=1, and a throughput ceiling of ~730-750 msg/s that does not improve with more concurrent clients because it's bound by the number of partitions × per-fsync latency, not by network or CPU capacity. This is a real, deliberate cost — not an oversight — paid in exchange for the guarantee that a producer receiving "OK" can trust the message actually survived a crash.

## Consequences
Any future work on relaxing this (batched fsync, configurable ACK policy per-topic, etc.) needs to be its own explicit decision with its own ADR, not a quiet performance optimization — because it changes what "OK" means to every producer using the system. This ADR exists specifically so that change, if it happens, has to consciously supersede this one rather than silently drift away from it.
