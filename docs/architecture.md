# Architecture

## Component responsibilities

**`broker_server_epoll.cpp`** — process entry point. Owns the listening socket, the single `epoll_wait` loop, the `ThreadPool`, and the `Broker`. Does not touch message content directly — decodes protocol bytes into commands (`PRODUCE`/`CONSUME`) and delegates everything else.

**`ThreadPool`** — fixed-size worker pool (`std::condition_variable` + `std::mutex` task queue). Workers are only engaged once `epoll` has confirmed real data is ready on a socket — this decouples "how many clients are connected" from "how many threads exist," which matters because a naive thread-per-connection model (Phase 4's `broker_server.cpp`, kept in the repo for comparison) does not scale past a few hundred connections.

**`Broker`** — owns `N` `Topic` instances (as `std::vector<std::unique_ptr<Topic>>`, since `Topic` contains a non-movable `std::mutex` and can't live directly in a resizable vector). Routes every `publish()` through `Partitioner` before handing the message to the target `Topic`.

**`Partitioner`** — `hash(key) % partition_count` for keyed messages (`std::hash<std::string>`); `std::atomic<size_t>` round-robin for unkeyed messages. Round-robin was chosen over `hash("")` deliberately — hashing an empty key would collapse every unkeyed message into the same single partition.

**`Topic`** — owns an in-memory `std::queue<Message>` plus one `Log`. `publish()` writes to the log *before* pushing to the in-memory queue (durability precedes visibility). The constructor replays the entire log file into the in-memory queue on startup — this is the actual recovery mechanism, not a separate "recovery module."

**`Log`** — append-only binary file, raw POSIX fd (`open`/`write`/`fsync`/`close`), one per partition. Its own dedicated `std::mutex`, deliberately separate from `Topic`'s mutex, so a slow disk `fsync()` doesn't serialize behind (or in front of) fast in-memory queue operations for unrelated work.

**`protocol.h`** — pure functions, no state. `encodePacket`/`decodePacket` handle the length-prefixed binary wire format. No class needed since framing has no persistent state between calls.

## Data flow: a single PRODUCE request

```
client                    epoll loop              worker thread              Topic          Log
  │  TCP connect               │                        │                     │              │
  │ ──────────────────────────>│ EPOLL_CTL_ADD           │                     │              │
  │  send(framed PRODUCE)      │                        │                     │              │
  │ ──────────────────────────>│ epoll_wait wakes        │                     │              │
  │                             │ EPOLLONESHOT fires      │                     │              │
  │                             │ ─── dispatch to pool ──>│                     │              │
  │                             │                        │ recv() into         │              │
  │                             │                        │  per-client buffer  │              │
  │                             │                        │ decodePacket()      │              │
  │                             │                        │ broker.publish() ──>│              │
  │                             │                        │                     │ log.append()─>│ write()+fsync()
  │                             │                        │                     │<─ returns ────│ (blocks until on-disk)
  │                             │                        │                     │ push to queue │
  │                             │                        │<── returns ─────────│              │
  │                             │                        │ encode "OK"         │              │
  │<──────────────── send(response) ──────────────────── │                     │              │
  │                             │                        │ rearm(client_fd) ──>│ EPOLL_CTL_MOD│
```

The response is only sent **after** `fsync()` returns — this is what makes the ACK-after-fsync durability guarantee (see [storage.md](storage.md)) real rather than aspirational.

## Why epoll instead of thread-per-connection

Phase 4 built a working thread-per-connection server first (`broker_server.cpp`, kept in the repo), then Phase 8 replaced it. The reason: thread-per-connection ties resource usage directly to connection count — 10,000 idle connections means 10,000 OS threads, each with their own stack, most doing nothing. `epoll` lets one thread monitor thousands of sockets and only wakes work up when there's actually data to process, which is why every real message broker (Kafka, RabbitMQ, etc.) uses an event-loop model at its core rather than thread-per-connection.

## Resource limits (Tier 4)

- Per-client buffer cap: a connection whose accumulated unparsed bytes exceed one maximum-size frame (9 + MAX_PAYLOAD_SIZE + 4) is disconnected and a client_buffer_exceeded_limit WARN is logged. Verified live under an AddressSanitizer build by sending 10MB+ of zero bytes.
- Connection cap: --max-connections (default 1000). Connections beyond the cap receive a framed "ERR max connections reached" message and are closed. sf_client peeks for this message right after connecting, because sending first races against the server's close and the resulting TCP RST discards the unread rejection.

Known remaining gap: worst-case memory is still connections x max frame size (about 10GB at the defaults) if every client sends junk just under the cap. Closing on the first bad magic number would remove this, but needs decodePacket to distinguish failure reasons (see protocol.md, known limitation).
