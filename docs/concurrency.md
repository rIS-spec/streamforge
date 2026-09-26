# Concurrency

## Locking model

| Resource | Lock | Why separate |
|---|---|---|
| `Topic::messages_` (in-memory queue) | `Topic`'s own `std::mutex` | Fast, in-memory operation |
| `Log`'s file descriptor | `Log`'s own `std::mutex` | Deliberately separate from `Topic`'s — a slow disk `fsync()` should not serialize behind or block unrelated fast queue operations on the same `Topic` (though in practice, `publish()` calls both in sequence under the same logical operation, so the separation matters most for any future code path that might read the queue without touching the log) |
| `client_buffers` (per-client byte buffers in `broker_server_epoll.cpp`) | Dedicated `buffers_mutex` | Buffers moved from per-connection-thread stack locals (Phase 4 model) into a shared `std::unordered_map<int, std::vector<uint8_t>>` in Phase 8, since they now must persist across separate, independent `ThreadPool` task dispatches rather than living inside one dedicated thread's call stack |

Each partition's `Topic`+`Log` pair is independently locked — there is no global broker-wide lock, which is what allows the 4-partition benchmark to sustain roughly 4x the throughput of what a single partition could achieve alone (see [benchmarking.md](benchmarking.md)).

## The `EPOLLONESHOT` bug (found and fixed during Phase 8)

**Symptom**: intermittent, silent failure — `test_client` produce/consume commands would occasionally return nothing, with no crash and no error message.

**Root cause**: `epoll` in its default level-triggered mode re-reports a socket as "ready" on *every* `epoll_wait()` call for as long as unread data remains on it — including while a worker thread from the previous `epoll_wait()` cycle was still in the middle of processing that same socket. This let two worker threads call `recv()` on the same client connection concurrently, interleaving bytes from what should have been two sequential reads and corrupting the length-prefixed framing.

**Fix**: register client sockets with `EPOLLONESHOT`. This tells the kernel to report the socket as ready exactly once, then disable further event notifications for it until explicitly re-armed. After a worker finishes processing a socket's available data, it calls `epoll_ctl(EPOLL_CTL_MOD, ...)` (wrapped in a `rearm()` helper) to re-enable notifications — only then can that socket be dispatched to a worker again.

**Why this matters beyond the fix itself**: this bug produced no compiler warning, no sanitizer flag (it's a logic race at the epoll-event level, not a data race ASan/TSan instruments for by default in this pattern), and no crash — only quietly wrong behavior. It was only caught by noticing the *symptom* (produce/consume returning nothing) and reasoning backward to "two threads must be touching the same socket." This is the kind of bug that's specific to event-loop + thread-pool hybrid architectures, and understanding *why* `EPOLLONESHOT` exists (not just that it fixes the symptom) is the actual lesson.

## Verified concurrency correctness

`TopicConcurrencyTest.HandlesManyProducersSafely` drives 10 threads × 1,000 messages = 10,000 concurrent `publish()` calls against a single mutex-protected `Topic`, asserting zero message loss. This test also runs clean under CI's AddressSanitizer + UndefinedBehaviorSanitizer build (see [`.github/workflows/ci.yml`](../.github/workflows/ci.yml)), which gives instrumented evidence — not just a passing assertion count — that no memory-safety issue surfaces under this concurrent load.

**Note on ThreadSanitizer**: TSan (which specifically detects data races) is not currently part of the CI pipeline — only ASan+UBSan are. Adding a dedicated TSan job is listed as future work, since ASan/UBSan do not substitute for genuine data-race detection.
