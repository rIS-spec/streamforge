# ADR 006: Per-resource locking instead of a single global broker lock

## Context
Multiple resources need concurrency protection: each `Topic`'s in-memory queue, each `Log`'s file descriptor, and the shared per-client byte-buffer map in `broker_server_epoll.cpp`. A single global mutex protecting the entire `Broker` would be the simplest possible approach.

## Decision
Each `Topic` has its own mutex; each `Log` has its own, separate mutex; the client-buffer map has its own dedicated mutex. There is no broker-wide global lock.

## Alternatives considered
- **A single global `Broker`-level mutex** — trivially correct (no possibility of forgetting to lock the right thing), but would serialize every operation across every partition, defeating the entire point of partitioning (ADR 005) — two producers writing to two different partitions would block each other for no reason.

## Trade-offs
Per-resource locking allows genuinely independent partitions to make progress concurrently — this is what lets the 4-partition benchmark sustain roughly 4x a single partition's fsync-bound ceiling. The cost is more mutexes to reason about and a real requirement to be careful about lock ordering if any code path ever needs to hold more than one at a time (currently, no code path does — each operation touches exactly one `Topic`+`Log` pair or the buffer map, never both categories at once, which avoids the classic multi-lock deadlock risk by construction rather than by discipline).

## Consequences
`Topic` and `Log` deliberately use *separate* mutexes rather than sharing one, even though `Topic::publish()` always calls both together — the reasoning being that a slow `fsync()` inside `Log` shouldn't need to hold `Topic`'s queue-access lock for its full duration if a future code path ever needed to read the queue without touching the log. As of the current code, no such path exists yet, so this separation is a design choice made for future flexibility rather than one that changes today's measured behavior.
