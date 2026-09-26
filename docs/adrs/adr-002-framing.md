# ADR 002: Length-prefixed binary framing

## Context
TCP is a byte stream with no concept of message boundaries — a single client `send()` can arrive across multiple server `recv()` calls, and multiple sends can coalesce into one `recv()`. The broker needs a way to know exactly where one logical message ends and the next begins.

## Decision
Every message on the wire (and in the on-disk log — see ADR 005) is framed as `[4-byte magic][4-byte length, network byte order][payload bytes]`.

## Alternatives considered
- **Delimiter-based framing** (e.g. newline-terminated) — simpler to implement, but requires escaping if the payload itself can contain the delimiter byte, and payloads here are meant to hold arbitrary binary data, not just text.
- **Fixed-size messages** — no framing logic needed at all, but wastes bandwidth on small messages and hard-caps message size at compile time.

## Trade-offs
Length-prefixing handles arbitrary binary payloads cleanly with no escaping, at the cost of needing an accumulating per-connection buffer (see ADR 003) and needing to bounds-check the length field before trusting it (a malicious or corrupted length could otherwise cause the broker to wait forever for data that will never arrive, or attempt a huge allocation).

## Consequences
The 10MB `MAX_PAYLOAD_SIZE` cap in `decodePacket()` exists specifically because of this trade-off — it's checked before the magic-number check, so a bad length field is rejected before it can influence any buffering or allocation decision. The magic number (`0x53465247`, "SFRG") exists to catch a desynchronized stream early, rather than only discovering corruption when a garbage length value produces an obviously wrong read.
