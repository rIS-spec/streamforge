#!/usr/bin/env bash
# Failure injection test suite. Usage: scripts/failure_injection.sh <build-dir> [port]
set -u

BUILD_DIR="${1:?usage: failure_injection.sh <build-dir> [port]}"
PORT="${2:-9700}"
FAIL=0

report() {
    if [ "$2" -eq 0 ]; then
        echo "PASS: $1"
    else
        echo "FAIL: $1"
        FAIL=1
    fi
}

echo "=== Scenario 1: kill -9 mid-write, then restart and check recovery ==="
DATA_DIR="$(mktemp -d)"
LOG="$DATA_DIR/broker.out"
"$BUILD_DIR/broker_server_epoll" --port "$PORT" --workers 2 --partitions 1 --data-dir "$DATA_DIR" >> "$LOG" 2>&1 &
BROKER_PID=$!
sleep 1
printf 'PRODUCE before-kill-1\nPRODUCE before-kill-2\nQUIT\n' | "$BUILD_DIR/sf_client" --port "$PORT" > /dev/null
kill -9 "$BROKER_PID" 2>/dev/null
wait "$BROKER_PID" 2>/dev/null
"$BUILD_DIR/broker_server_epoll" --port "$PORT" --workers 2 --partitions 1 --data-dir "$DATA_DIR" >> "$LOG" 2>&1 &
BROKER_PID=$!
sleep 1
OUT="$(printf 'CONSUME 0\nCONSUME 0\nQUIT\n' | "$BUILD_DIR/sf_client" --port "$PORT")"
echo "$OUT" | grep -q "before-kill-1" && echo "$OUT" | grep -q "before-kill-2"
report "messages written before kill -9 are recovered after restart" $?
kill "$BROKER_PID" 2>/dev/null; wait "$BROKER_PID" 2>/dev/null
rm -rf "$DATA_DIR"

echo
echo "=== Scenario 2: corrupt the on-disk log while the broker is down ==="
DATA_DIR="$(mktemp -d)"
LOG="$DATA_DIR/broker.out"
"$BUILD_DIR/broker_server_epoll" --port "$PORT" --workers 2 --partitions 1 --data-dir "$DATA_DIR" >> "$LOG" 2>&1 &
BROKER_PID=$!
sleep 1
printf 'PRODUCE will-survive\nQUIT\n' | "$BUILD_DIR/sf_client" --port "$PORT" > /dev/null
kill "$BROKER_PID"; wait "$BROKER_PID" 2>/dev/null
head -c 37 /dev/urandom >> "$DATA_DIR/streamforge_p0.log"
"$BUILD_DIR/broker_server_epoll" --port "$PORT" --workers 2 --partitions 1 --data-dir "$DATA_DIR" >> "$LOG" 2>&1 &
BROKER_PID=$!
sleep 1
kill -0 "$BROKER_PID" 2>/dev/null
report "broker starts successfully against a log with a corrupted/truncated tail record" $?
OUT="$(printf 'CONSUME 0\nQUIT\n' | "$BUILD_DIR/sf_client" --port "$PORT")"
echo "$OUT" | grep -q "will-survive"
report "the earlier valid record still replays correctly despite the corrupted tail" $?
kill "$BROKER_PID" 2>/dev/null; wait "$BROKER_PID" 2>/dev/null
rm -rf "$DATA_DIR"

echo
echo "=== Scenario 3: flood a live broker with garbage, then confirm it still serves ==="
DATA_DIR="$(mktemp -d)"
LOG="$DATA_DIR/broker.out"
"$BUILD_DIR/broker_server_epoll" --port "$PORT" --workers 2 --partitions 1 --data-dir "$DATA_DIR" >> "$LOG" 2>&1 &
BROKER_PID=$!
sleep 1
for i in 1 2 3 4 5; do
    python3 -c "
import socket
s = socket.create_connection(('127.0.0.1', $PORT))
s.sendall(bytes([$i]) * 200)
s.close()
" 2>/dev/null
done
sleep 1
OUT="$(printf 'HEALTH\nQUIT\n' | timeout 5 "$BUILD_DIR/sf_client" --port "$PORT")"
echo "$OUT" | grep -q "HEALTHY"
report "broker still answers HEALTH after 5 garbage connections" $?
kill "$BROKER_PID" 2>/dev/null; wait "$BROKER_PID" 2>/dev/null
rm -rf "$DATA_DIR"

echo
if [ "$FAIL" -eq 0 ]; then
    echo "ALL FAILURE-INJECTION SCENARIOS PASSED"
fi
exit $FAIL
