#!/usr/bin/env bash
set -e
echo "=============================================="
echo " StreamForge — End-to-End Demo"
echo "=============================================="

PORT=9900
DATA_DIR="$(mktemp -d)"

echo
echo "--- 1. Start the broker natively ---"
pkill -f broker_server_epoll 2>/dev/null || true
sleep 1
build/broker_server_epoll --port $PORT --workers 4 --partitions 1 --data-dir "$DATA_DIR" &
BROKER_PID=$!
sleep 1

echo
echo "--- 2. Produce messages, consume one, check HEALTH/METRICS (real traffic) ---"
build/sf_client --port $PORT << 'CMDS'
PRODUCE order-42 shipped
PRODUCE order-43 shipped
PRODUCE order-44 shipped
CONSUME 0
HEALTH
METRICS
QUIT
CMDS

echo
echo "--- 3. Hard-kill the broker mid-session (kill -9, not graceful) to prove durability ---"
kill -9 $BROKER_PID
wait $BROKER_PID 2>/dev/null || true
echo "broker killed. restarting..."
build/broker_server_epoll --port $PORT --workers 4 --partitions 1 --data-dir "$DATA_DIR" &
BROKER_PID=$!
sleep 1

echo
echo "--- 4. Confirm the un-consumed messages survived the hard kill ---"
build/sf_client --port $PORT << 'CMDS'
CONSUME 0
CONSUME 0
METRICS
QUIT
CMDS

echo
echo "--- 5. Quick measured benchmark against the recovered broker ---"
build/benchmark_client --port $PORT --concurrency 10 --messages 100 --payload-size 100

echo
echo "--- 6. CI status (GitHub Actions, real pipeline) ---"
gh run list --limit 4

echo
echo "--- 7. Full test suite, one more time, for the record ---"
(cd build && ctest --output-on-failure 2>&1 | tail -5)

echo
echo "=============================================="
echo " Demo complete."
echo "=============================================="
kill $BROKER_PID 2>/dev/null || true
wait $BROKER_PID 2>/dev/null || true
rm -rf "$DATA_DIR"
