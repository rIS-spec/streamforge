#!/usr/bin/env bash
# Server-level smoke test. Usage: scripts/smoke_test.sh <build-dir> [port]
# Starts broker_server_epoll from <build-dir>, drives it over real TCP,
# then checks it shuts down cleanly with no sanitizer report.
set -u

BUILD_DIR="${1:?usage: smoke_test.sh <build-dir> [port]}"
PORT="${2:-9123}"
DATA_DIR="$(mktemp -d)"
LOG="$DATA_DIR/broker.out"

fail() {
    echo "SMOKE FAIL: $1"
    echo "--- broker output ---"
    cat "$LOG"
    kill "$BROKER_PID" 2>/dev/null
    exit 1
}

"$BUILD_DIR/broker_server_epoll" --port "$PORT" --workers 2 --partitions 2 \
    --data-dir "$DATA_DIR" >"$LOG" 2>&1 &
BROKER_PID=$!
sleep 1
kill -0 "$BROKER_PID" 2>/dev/null || fail "broker did not start"

# 1. Valid traffic: produce/consume round trip and health check
OUT="$(printf 'PRODUCE hello\nCONSUME 0\nHEALTH\nQUIT\n' | "$BUILD_DIR/sf_client" --port "$PORT")"
echo "$OUT" | grep -q "MSG: hello" || fail "produce/consume round trip"
echo "$OUT" | grep -q "HEALTHY" || fail "HEALTH command"

# 2. Hostile traffic: more zero bytes than one legal frame can ever need
python3 - "$PORT" <<'PY'
import socket, sys, time
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])))
try:
    s.sendall(b"\x00" * (10 * 1024 * 1024 + 100))
except OSError:
    pass  # the server may close mid-send once the cap trips: expected
time.sleep(1)
PY
grep -q client_buffer_exceeded_limit "$LOG" || fail "oversized garbage was not rejected"

# 3. Still serving after the hostile client
OUT="$(printf 'HEALTH\nQUIT\n' | "$BUILD_DIR/sf_client" --port "$PORT")"
echo "$OUT" | grep -q "HEALTHY" || fail "broker unhealthy after hostile client"

# 4. Clean shutdown, and no sanitizer output
kill -TERM "$BROKER_PID"
wait "$BROKER_PID"
RC=$?
[ "$RC" -eq 0 ] || fail "broker exited with code $RC (crash or sanitizer error)"
if grep -Eq "AddressSanitizer|ThreadSanitizer|LeakSanitizer|runtime error" "$LOG"; then
    fail "sanitizer report found"
fi

echo "SMOKE PASS ($BUILD_DIR)"
rm -rf "$DATA_DIR"
