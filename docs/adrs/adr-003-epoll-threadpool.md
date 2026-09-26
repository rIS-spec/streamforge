# ADR 003: Fixed ThreadPool dispatched via epoll, not thread-per-connection

## Context
The broker needs to handle many concurrent client connections. Phase 4 initially implemented thread-per-connection (one dedicated OS thread per accepted socket, blocking on `recv()`).

## Decision
Replace thread-per-connection with a single-threaded `epoll_wait` loop that dispatches ready sockets to a small fixed-size `ThreadPool` via `EPOLLONESHOT`.

## Alternatives considered
- **Thread-per-connection** (the original Phase 4 implementation, kept in the repo as `broker_server.cpp` for comparison) — simplest mental model, works correctly, but ties OS thread count directly to connection count. Thousands of idle connections would mean thousands of idle OS threads, each with its own stack — real resource cost for no real work being done.
- **One thread per CPU core, no pool** — considered and rejected; a fixed pool with a task queue is a more standard, more tunable pattern (`--workers` is a runtime config flag, not a compile-time constant).

## Trade-offs
Thread-per-connection is dramatically simpler to reason about (each connection's state lives naturally on its own thread's stack) but doesn't scale. The epoll+pool model scales connection count independently of thread count, but requires explicit state management: per-client byte buffers can no longer live as thread-local stack variables (a single worker thread handles many different clients over its lifetime), so they were moved into a shared `std::unordered_map<int, std::vector<uint8_t>>` guarded by its own mutex (see ADR 006 / `docs/concurrency.md`).

## Consequences
This decision directly caused the `EPOLLONESHOT` race bug documented in `docs/concurrency.md`: level-triggered epoll re-reports a ready socket on every `epoll_wait()` call even while a worker from a previous cycle is still processing it, which let two workers `recv()` the same connection concurrently and corrupt the framing. `EPOLLONESHOT` plus an explicit re-arm step after each worker finishes was required to fix it — a subtlety that thread-per-connection would never have exposed, since each connection only ever had one thread touching it.
