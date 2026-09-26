# ADR 004: Append-only logs for durable storage

## Context
Messages need to survive a broker restart. The storage layer needed to decide between an append-only log (write new data at the end, never modify existing bytes) and an updatable/indexed store (e.g. a B-tree-backed file, or per-message files).

## Decision
Each partition is backed by a single append-only binary log file. `Log::append()` only ever writes at the current end of the file (`O_APPEND`); nothing is ever rewritten or deleted in place.

## Alternatives considered
- **Per-message files** — trivially simple, but filesystem overhead (one `open`/`close` per message) would be far worse for throughput, and directory listing at scale becomes a real bottleneck.
- **In-place updatable structure** (e.g. a B-tree or hash-indexed file) — would allow true deletion/offset-tracking, but is a fundamentally harder, higher-risk data structure to implement correctly, especially for crash-safety (a half-written B-tree node is much worse than a half-written appended record — see ADR 007/recovery).

## Trade-offs
Append-only writes are sequential, which is exactly the access pattern that makes `fsync()` on a spinning or SSD disk predictable and fast relative to random writes — this is the same reason Kafka itself uses append-only segment files. The cost: there is no way to delete or overwrite an individual message in place. Consuming a message today only removes it from the in-memory queue; the log itself is untouched (see the "no consumer-offset tracking" limitation in `docs/storage.md`).

## Consequences
Because there's no in-place update, "has this message been consumed" is not a property the log itself can currently answer — that information only exists in the in-memory queue, which is lost on restart. This is the direct cause of the documented replay-on-restart limitation. A real offset-tracking mechanism (a separate small file or a reserved region recording "last consumed offset per partition") is listed as future work rather than solved here, since it's a genuinely separate design decision from "how do we store messages durably" — this ADR is scoped to that narrower question only.
