# ADR 001: TCP for broker transport

## Context
StreamForge needs a transport protocol between producers/consumers and the broker. The two realistic options at the socket layer are TCP (`SOCK_STREAM`) and UDP (`SOCK_DGRAM`).

## Decision
Use TCP.

## Alternatives considered
- **UDP** — lower per-packet overhead, no connection setup cost, but no delivery guarantee, no ordering guarantee, and a 64KB practical payload ceiling without application-level fragmentation.

## Trade-offs
TCP gives in-order, reliable, connection-oriented delivery for free at the transport layer — exactly the properties a message broker's producer/consumer link needs (a dropped or reordered `PRODUCE` request would be a correctness bug, not a performance nuisance). The cost is connection-setup overhead (three-way handshake) and head-of-line blocking within a single TCP stream, neither of which matters at this project's scale — the benchmarking in `docs/benchmarking.md` shows the real bottleneck is `fsync()`-bound durability, not connection overhead.

## Consequences
Because TCP is a byte stream with no built-in message boundaries, StreamForge has to implement its own framing on top of it — this is the direct reason `protocol.h`'s length-prefixed format exists (see ADR 002). If UDP had been chosen instead, framing would be unnecessary (each datagram is already a discrete unit) but the application would then need to build its own retransmission and ordering logic — a strictly harder problem than framing.
