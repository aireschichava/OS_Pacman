#!/bin/bash
# Test 1: SIGUSR1 Score Logging Test
# Tests that the server creates score_log.txt when receiving SIGUSR1

echo "=== TEST 1: SIGUSR1 Score Logging ==="

# Start server in background
cd Project_2
rm -f score_log.txt
bin/PacmanIST levels 3 /tmp/test_server &
SERVER_PID=$!
sleep 1

echo "Server started with PID: $SERVER_PID"

# Send SIGUSR1 to trigger score logging
echo "Sending SIGUSR1 to server..."
kill -SIGUSR1 $SERVER_PID
sleep 1

# Check if log file was created
if [ -f "score_log.txt" ]; then
    echo "✅ PASS: score_log.txt created!"
    cat score_log.txt
else
    echo "❌ FAIL: score_log.txt not found"
fi

# Cleanup
kill $SERVER_PID 2>/dev/null
rm -f /tmp/test_server
echo ""

# Test 2: SIGPIPE Survival Test
echo "=== TEST 2: SIGPIPE Survival ==="

bin/PacmanIST levels 2 /tmp/test_server2 &
SERVER_PID=$!
sleep 1

# Start a client
bin/client /tmp/test_server2 &
CLIENT_PID=$!
sleep 2

echo "Server PID: $SERVER_PID, Client PID: $CLIENT_PID"
echo "Killing client abruptly (simulating SIGPIPE)..."
kill -9 $CLIENT_PID 2>/dev/null
sleep 1

# Check if server is still running
if ps -p $SERVER_PID > /dev/null 2>&1; then
    echo "✅ PASS: Server survived SIGPIPE!"
else
    echo "❌ FAIL: Server crashed after client death"
fi

# Cleanup
kill $SERVER_PID 2>/dev/null
rm -f /tmp/test_server2

echo ""
echo "=== Tests Complete ==="
