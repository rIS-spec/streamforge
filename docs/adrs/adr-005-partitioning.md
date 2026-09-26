# ADR 005: Hash-based partitioning for keyed messages, round-robin for unkeyed

## Context
A single `Topic`+`Log` pair is fundamentally limited to one writer thread's worth of sequential throughput (see ADR 004, and the fsync-bound bottleneck in `docs/benchmarking.md`). The broker needs a way to spread messages across multiple independent partitions while still giving callers a way to control ordering guarantees when they need them.

## Decision
`Partitioner` routes each message using `hash(key) % partition_count` (`std::hash<std::string>`) when a key is present, and round-robin (`std::atomic<size_t>` fetch-add) when the key is empty.

## Alternatives considered
- **`hash("")` for unkeyed messages** — considered and explicitly rejected. Hashing an empty string always produces the same hash value, which would route every unkeyed message to the same single partition, defeating the entire purpose of partitioning for exactly the traffic pattern (no key specified) most likely to be common.
- **Pure round-robin for everything, no key-based routing at all** — simpler, but loses the ability to guarantee all messages sharing a key land in the same partition (and therefore the same total order relative to each other), which is a property real consumers of a keyed stream (e.g. "all events for device-001, in order") depend on.

## Trade-offs
Hash-based routing gives same-key-same-partition ordering for free, verified directly by `BrokerTest.SameKeyAlwaysGoesToSamePartition`. The cost is that partition count becomes effectively fixed once chosen — `hash(key) % N` changes its output distribution if `N` changes, meaning a future partition-count change would silently redistribute existing keys to different partitions than they were originally written to (a real operational hazard in any system using modulo-based hash routing, including Kafka's own default partitioner).

## Consequences
Because `Broker`'s constructor takes `partition_count` as a fixed value at startup (no live repartitioning), this hazard is currently avoided by simply not supporting a partition-count change on a live system — restarting with a different `--partitions` value would be a breaking change to key routing. This constraint is not yet explicitly enforced in code (nothing currently warns or errors if you restart with a different partition count against an existing data directory) — listed here as a real gap between the design's implicit assumption and what the code actually checks.
