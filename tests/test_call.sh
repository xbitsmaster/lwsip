#!/bin/bash

echo "========================================"
echo "lwsip Call Test - caller/callee"
echo "========================================"
echo ""

# Clean up any existing processes
pkill -f sip_fake || true
pkill -f callee || true
pkill -f caller || true
sleep 1

# Clean log files
rm -f /tmp/sip_fake.log /tmp/callee.log /tmp/caller.log

echo "[1/4] Starting SIP fake server on port 5060..."
./sip_fake > /tmp/sip_fake.log 2>&1 &
SIP_FAKE_PID=$!
echo "      SIP fake server PID: $SIP_FAKE_PID"
sleep 2

# Verify SIP fake server is running
if ! ps -p $SIP_FAKE_PID > /dev/null; then
    echo "      ✗ SIP fake server failed to start"
    cat /tmp/sip_fake.log
    exit 1
fi
echo "      ✓ SIP fake server started"
echo ""

echo "[2/4] Starting callee (user 1000)..."
./callee > /tmp/callee.log 2>&1 &
CALLEE_PID=$!
echo "      Callee PID: $CALLEE_PID"
sleep 3

# Check callee registration
if grep -q "Registered successfully" /tmp/callee.log; then
    echo "      ✓ Callee registered successfully"
elif grep -q "Registration result: SUCCESS" /tmp/callee.log; then
    echo "      ✓ Callee registered successfully"
else
    echo "      - Callee registration status:"
    grep -i "register" /tmp/callee.log | tail -3
fi
echo ""

echo "[3/4] Starting caller (user 1001 -> 1000)..."
./caller > /tmp/caller.log 2>&1 &
CALLER_PID=$!
echo "      Caller PID: $CALLER_PID"
echo ""

echo "[4/4] Waiting for call to complete (15 seconds)..."
sleep 15
echo ""

echo "========================================"
echo "Test Results"
echo "========================================"
echo ""

# Check if processes are still running
CALLEE_RUNNING=0
CALLER_RUNNING=0

if ps -p $CALLEE_PID > /dev/null 2>&1; then
    CALLEE_RUNNING=1
fi

if ps -p $CALLER_PID > /dev/null 2>&1; then
    CALLER_RUNNING=1
fi

echo "Process Status:"
echo "  SIP fake server: $(ps -p $SIP_FAKE_PID > /dev/null 2>&1 && echo 'Running' || echo 'Stopped')"
echo "  Callee: $([ $CALLEE_RUNNING -eq 1 ] && echo 'Running' || echo 'Stopped')"
echo "  Caller: $([ $CALLER_RUNNING -eq 1 ] && echo 'Running' || echo 'Stopped')"
echo ""

echo "=== Callee Log Analysis ==="
if grep -q "Registration result: SUCCESS" /tmp/callee.log; then
    echo "  ✓ Registration successful"
else
    echo "  ✗ Registration failed or not completed"
fi

if grep -q "Incoming call from" /tmp/callee.log; then
    echo "  ✓ Received incoming call"
else
    echo "  - No incoming call detected"
fi

if grep -q "Call established" /tmp/callee.log; then
    echo "  ✓ Call established"
else
    echo "  - Call not established"
fi

echo ""
echo "=== Caller Log Analysis ==="
if grep -q "Registration result: SUCCESS" /tmp/caller.log; then
    echo "  ✓ Registration successful"
else
    echo "  ✗ Registration failed or not completed"
fi

if grep -q "Call initiated successfully" /tmp/caller.log; then
    echo "  ✓ Call initiated"
else
    echo "  - Call not initiated"
fi

if grep -q "Call established" /tmp/caller.log; then
    echo "  ✓ Call established"
else
    echo "  - Call not established"
fi

echo ""
echo "=== Error Check ==="
CALLEE_ERRORS=$(grep -i "error\|failed" /tmp/callee.log | grep -v "ERROR: Invalid" | wc -l | tr -d ' ')
CALLER_ERRORS=$(grep -i "error\|failed" /tmp/caller.log | grep -v "ERROR: Invalid" | wc -l | tr -d ' ')

echo "  Callee errors: $CALLEE_ERRORS"
echo "  Caller errors: $CALLER_ERRORS"

if [ $CALLEE_ERRORS -gt 0 ]; then
    echo ""
    echo "  Callee error messages:"
    grep -i "error\|failed" /tmp/callee.log | grep -v "ERROR: Invalid" | head -5
fi

if [ $CALLER_ERRORS -gt 0 ]; then
    echo ""
    echo "  Caller error messages:"
    grep -i "error\|failed" /tmp/caller.log | grep -v "ERROR: Invalid" | head -5
fi

echo ""
echo "========================================"
echo "Cleaning up..."
echo "========================================"

# Kill processes
kill $SIP_FAKE_PID 2>/dev/null || true
kill $CALLEE_PID 2>/dev/null || true
kill $CALLER_PID 2>/dev/null || true

sleep 1

echo ""
echo "Test completed. Log files:"
echo "  SIP fake: /tmp/sip_fake.log"
echo "  Callee:   /tmp/callee.log"
echo "  Caller:   /tmp/caller.log"
echo ""
