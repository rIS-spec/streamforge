# Wire Protocol

## Frame format

```
┌──────────────┬──────────────┬─────────────────────┐
│ magic (4B)   │ length (4B)  │ payload (length B)   │
│ 0x53465247   │ uint32, NBO  │ raw bytes            │
│ "SFRG"       │              │                      │
└──────────────┴──────────────┴─────────────────────┘
```

- **Magic number**: `0x53465247` — ASCII "SFRG". First check on every decode; catches a desynced stream (e.g. a client speaking a different or corrupted protocol) before we trust the length field at all.
- **Length**: `uint32_t`, network byte order (`htonl`/`ntohl`). Capped at 10MB (`MAX_PAYLOAD_SIZE` in `protocol.h`) — this cap is checked **before** the magic-number check triggers a read of that many bytes, specifically so a malicious or corrupted length field can't make the broker allocate or wait for an unbounded amount of data.
- **Payload**: raw bytes, application-defined content (currently ASCII commands like `PRODUCE <text>` or `CONSUME <partition>`).

## `decodePacket` semantics

```cpp
struct DecodedPacket {
    bool valid;
    std::string payload;
};
```

Returns `{false, ""}` — never throws, never reads out of bounds — for any of:
1. Fewer than 8 bytes available (can't even read magic+length yet)
2. Declared length exceeds `MAX_PAYLOAD_SIZE`
3. Magic number doesn't match
4. Available bytes are fewer than `8 + length` (payload not fully arrived yet)

**Known, documented limitation**: all four cases collapse into the same `valid = false` — there's no way for a caller to distinguish "wait for more data" from "this stream is corrupted, give up." A production version would use an enum (`Incomplete`, `BadMagic`, `Oversized`, `Valid`) so the caller could decide differently per case (e.g. close the connection immediately on `BadMagic`, but keep buffering on `Incomplete`). Not yet implemented — listed honestly rather than silently worked around.

## Why length-prefixed framing over TCP

TCP is a byte stream, not a message stream — a single `send()` on the client can arrive as multiple `recv()`s on the server, and multiple `send()`s can coalesce into one `recv()`. Without explicit framing, the server has no way to know where one logical message ends and the next begins. This is why `broker_server_epoll.cpp` keeps a **persistent per-client `std::vector<uint8_t>` buffer** that accumulates across `recv()` calls — the inner loop decodes as many complete packets as are already buffered and erases only the consumed bytes, leaving any partial trailing packet intact for the next `recv()`.

## Commands (application layer, not part of the framing itself)

| Command | Format | Response |
|---|---|---|
| Produce | `PRODUCE <payload>` | `OK` |
| Consume | `CONSUME <partition_idx>` | `MSG: <payload>` or `EMPTY` |
| Unknown | anything else | `ERR unknown command` |

**Known limitation**: the server only populates `Message::payload` from the command string — `key`, `topic`, `partition`, `offset`, `timestamp`, and `producer_id` fields exist in the `Message` struct but are never set by the current protocol. A real key-based `PRODUCE key:value` syntax (needed to actually exercise the hash-partitioning logic over the network, not just in unit tests) is listed as future work.
