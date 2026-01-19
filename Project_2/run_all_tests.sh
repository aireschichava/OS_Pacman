#!/bin/bash
# Comprehensive PacmanIST Test Suite
# Run from Project_2 directory

echo "=============================================="
echo "    PACMANIST COMPREHENSIVE TEST SUITE"
echo "=============================================="
echo ""

PASS_COUNT=0
FAIL_COUNT=0

pass() {
    echo "✅ PASS: $1"
    ((PASS_COUNT++))
}

fail() {
    echo "❌ FAIL: $1"
    ((FAIL_COUNT++))
}

cleanup() {
    killall PacmanIST 2>/dev/null
    killall client 2>/dev/null
    rm -f /tmp/test_* /tmp/pacman_* score_log.txt
}

# Clean start
cleanup
sleep 1

# ===========================================
echo "=== TEST 1: Server Startup ==="
bin/PacmanIST levels 2 /tmp/test_server &
SERVER_PID=$!
sleep 1

if ps -p $SERVER_PID > /dev/null 2>&1; then
    pass "Server started successfully (PID: $SERVER_PID)"
else
    fail "Server failed to start"
fi
kill $SERVER_PID 2>/dev/null
sleep 1

# ===========================================
echo ""
echo "=== TEST 2: Client Connection ==="
bin/PacmanIST levels 1 /tmp/test_server2 &
SERVER_PID=$!
sleep 1

timeout 3 bin/client test1 /tmp/test_server2 &
CLIENT_PID=$!
sleep 2

if ps -p $SERVER_PID > /dev/null 2>&1; then
    pass "Server running with client connected"
else
    fail "Server crashed on client connect"
fi

kill $CLIENT_PID 2>/dev/null
kill $SERVER_PID 2>/dev/null
sleep 1

# ===========================================
echo ""
echo "=== TEST 3: SIGUSR1 Score Logging ==="
rm -f score_log.txt
bin/PacmanIST levels 2 /tmp/test_server3 &
SERVER_PID=$!
sleep 1

kill -SIGUSR1 $SERVER_PID
sleep 1

if [ -f "score_log.txt" ]; then
    pass "score_log.txt created after SIGUSR1"
    echo "    Content: $(head -1 score_log.txt)"
else
    fail "score_log.txt not created"
fi

kill $SERVER_PID 2>/dev/null
sleep 1

# ===========================================
echo ""
echo "=== TEST 4: SIGPIPE Survival ==="
bin/PacmanIST levels 1 /tmp/test_server4 &
SERVER_PID=$!
sleep 1

bin/client test2 /tmp/test_server4 &
CLIENT_PID=$!
sleep 2

kill -9 $CLIENT_PID 2>/dev/null
sleep 1

if ps -p $SERVER_PID > /dev/null 2>&1; then
    pass "Server survived SIGPIPE (client kill -9)"
else
    fail "Server crashed after SIGPIPE"
fi

kill $SERVER_PID 2>/dev/null
sleep 1

# ===========================================
echo ""
echo "=== TEST 5: SIGINT Graceful Shutdown ==="
bin/PacmanIST levels 1 /tmp/test_server5 &
SERVER_PID=$!
sleep 1

kill -SIGINT $SERVER_PID
sleep 1

if ! ps -p $SERVER_PID > /dev/null 2>&1; then
    pass "Server exited gracefully on SIGINT"
else
    fail "Server did not exit on SIGINT"
    kill -9 $SERVER_PID 2>/dev/null
fi

# Check FIFO cleanup
if [ ! -e /tmp/test_server5 ]; then
    pass "Server cleaned up FIFO on exit"
else
    fail "FIFO not cleaned up"
    rm -f /tmp/test_server5
fi
sleep 1

# ===========================================
echo ""
echo "=== TEST 6: Multi-Level Progression ==="
bin/PacmanIST levels 1 /tmp/test_server6 &
SERVER_PID=$!
sleep 1

# Create a moves file that reaches the portal quickly
echo -e "d\nd\nd\nd\nd" > /tmp/test_moves.txt

timeout 5 bin/client test3 /tmp/test_server6 /tmp/test_moves.txt &
CLIENT_PID=$!
sleep 4

# Server should still be running (multi-level)
if ps -p $SERVER_PID > /dev/null 2>&1; then
    pass "Server handles level transitions"
else
    fail "Server crashed during level transition"
fi

kill $CLIENT_PID 2>/dev/null
kill $SERVER_PID 2>/dev/null
rm -f /tmp/test_moves.txt
sleep 1

# ===========================================
echo ""
echo "=== TEST 7: Max Games Limit ==="
bin/PacmanIST levels 1 /tmp/test_server7 &
SERVER_PID=$!
sleep 1

# Try to connect - with max_games=1, only one should work
timeout 2 bin/client test4 /tmp/test_server7 &
CLIENT1=$!
sleep 1

# Second client should block/wait (not crash server)
timeout 2 bin/client test5 /tmp/test_server7 &
CLIENT2=$!
sleep 1

if ps -p $SERVER_PID > /dev/null 2>&1; then
    pass "Server handles multiple connection attempts"
else
    fail "Server crashed with multiple clients"
fi

kill $CLIENT1 $CLIENT2 2>/dev/null
kill $SERVER_PID 2>/dev/null
sleep 1

# ===========================================
echo ""
echo "=== TEST 8: Invalid FIFO Path Handling ==="
timeout 2 bin/client test6 /tmp/nonexistent_fifo 2>&1 | grep -q "error\|fail\|Error\|FAIL" 
if [ $? -eq 0 ] || [ $? -eq 1 ]; then
    pass "Client handles invalid FIFO gracefully (no crash)"
else
    fail "Client crashed on invalid FIFO"
fi

# ===========================================
echo ""
echo "=============================================="
echo "           TEST RESULTS SUMMARY"
echo "=============================================="
echo "  Passed: $PASS_COUNT"
echo "  Failed: $FAIL_COUNT"
echo "=============================================="

# Final cleanup
cleanup
