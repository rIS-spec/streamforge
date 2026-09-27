# Wire Protocol

## Frame format

```
┌──────────────┬──────────────┬───────────────────┬──────────────┐
│ magic (4B)   │ length (4B)  │ payload (length B) │ crc32 (4B)   │
│ 0x53465247   │ uint32, NBO  │ raw bytes          │ uint32, NBO  │
│ "SFRG"       │              │                    │ of payload   │
└──────────────┴──────────────┴───────────────────┴──────────────┘
```


- **Magic number**: `0x53465247` — ASCII "SFRG". First check on every decode; catches a desynced stream (e.g. a client speaking a different or corrupted protocol) before we trust anything else in the frame.
- **Version** (Tier 4): `uint8_t`, currently `PROTOCOL_VERSION = 1`. Checked immediately after the magic number, before the length field is even read. Exists so a future breaking wire-format change can be detected on decode — a client or broker speaking an incompatible version is rejected explicitly (`ProtocolTest.MismatchedProtocolVersionIsRejected`) rather than having its bytes silently misinterpreted against the wrong format.
- **Length**: `uint32_t`, network byte order (`htonl`/`ntohl`). Capped at 10MB (`MAX_PAYLOAD_SIZE` in `protocol.h`) — this cap is checked before the length can influence any allocation or read size.
- **Payload**: raw bytes, application-defined content (currently ASCII commands like `PRODUCE <text>` or `CONSUME <partition>`).
- **CRC32** (Tier 4): standard IEEE 802.3 polynomial, computed over the payload only, appended after it. `decodePacket()` recomputes this on decode and rejects the frame if it doesn't match.

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
4. Available bytes are fewer than `8 + length + 4` (payload + trailing CRC not fully arrived yet)
5. Recomputed CRC32 doesn't match the stored CRC32 (payload corruption — e.g. a single bit-flip) — verified by `ProtocolTest.CorruptedPayloadFailsChecksum`

**Known, documented limitation**: all five cases collapse into the same `valid = false` — there's no way for a caller to distinguish "wait for more data" from "this stream is corrupted, give up" from "checksum mismatch." A production version would use an enum (`Incomplete`, `BadMagic`, `Oversized`, `ChecksumMismatch`, `Valid`) so the caller could decide differently per case (e.g. close the connection immediately on `BadMagic` or `ChecksumMismatch`, but keep buffering on `Incomplete`). Not yet implemented — listed honestly rather than silently worked around.

## Why length-prefixed framing over TCP

TCP is a byte stream, not a message stream — a single `send()` on the client can arrive as multiple `recv()`s on the server, and multiple `send()`s can coalesce into one `recv()`. Without explicit framing, the server has no way to know where one logical message ends and the next begins. This is why `broker_server_epoll.cpp` keeps a **persistent per-client `std::vector<uint8_t>` buffer** that accumulates across `recv()` calls — the inner loop decodes as many complete packets as are already buffered and erases only the consumed bytes, leaving any partial trailing packet intact for the next `recv()`.

## Commands (application layer, not part of the framing itself)

| Command | Format | Response |
|---|---|---|
| Produce | `PRODUCE <payload>` | `OK` |
| Consume | `CONSUME <partition_idx>` | `MSG: <payload>` or `EMPTY` |
| Health (Tier 4) | `HEALTH` | `HEALTHY` |
| Metrics (Tier 4) | `METRICS` | JSON: `messages_produced`, `messages_consumed`, `connections_total`, `uptime_seconds` |
| Unknown | anything else | `ERR unknown command` |

**Note on naming**: these are not literal HTTP `/health`/`/metrics` endpoints — that would require a second listener speaking HTTP on a separate port, which was deliberately not built (this project has one protocol, built deeply, rather than two built shallowly). `HEALTH`/`METRICS` are commands on the existing binary protocol, reachable via `sf_client` exactly like `PRODUCE`/`CONSUME`.

**Known limitation**: the server only populates `Message::payload` from the command string — `key`, `topic`, `partition`, `offset`, `timestamp`, and `producer_id` fields exist in the `Message` struct but are never set by the current protocol. A real key-based `PRODUCE key:value` syntax (needed to actually exercise the hash-partitioning logic over the network, not just in unit tests) is listed as future work.