#!/bin/bash

echo "========================================"
echo "SIP Fake Scenario Testing"
echo "========================================"
echo ""

# Test scenarios
scenarios=(
    "router:Router mode (forward messages)"
    "normal:UAS simulation - normal (200 OK)"
    "busy:UAS simulation - busy (486)"
    "reject:UAS simulation - reject (603)"
    "timeout:UAS simulation - timeout (no response)"
    "unavailable:UAS simulation - unavailable (480)"
)

# Cleanup function
cleanup() {
    pkill -f sip_fake 2>/dev/null || true
    pkill -f "./caller" 2>/dev/null || true
    sleep 1
}

# Test counter
total_tests=0
passed_tests=0

# Test each scenario
for scenario_info in "${scenarios[@]}"; do
    IFS=':' read -r scenario desc <<< "$scenario_info"

    total_tests=$((total_tests + 1))
    echo "========================================"
    echo "Test $total_tests: $desc"
    echo "========================================"
    echo ""

    # Cleanup before test
    cleanup

    # Start sip_fake with scenario
    if [ "$scenario" == "router" ]; then
        # Router mode - no scenario argument
        echo "[1/3] Starting sip_fake in router mode..."
        ./sip_fake > /tmp/sip_fake_test.log 2>&1 &
    else
        # UAS simulation mode
        echo "[1/3] Starting sip_fake with --scenario=$scenario..."
        ./sip_fake --scenario=$scenario > /tmp/sip_fake_test.log 2>&1 &
    fi

    SIP_FAKE_PID=$!
    echo "      sip_fake PID: $SIP_FAKE_PID"

    # Wait for sip_fake to start
    sleep 2

    # Check if sip_fake is running
    if ! ps -p $SIP_FAKE_PID > /dev/null 2>&1; then
        echo "      ✗ sip_fake failed to start"
        echo ""
        echo "Log:"
        cat /tmp/sip_fake_test.log
        echo ""
        continue
    fi

    echo "      ✓ sip_fake started successfully"
    echo ""

    # Display the log to show mode
    echo "[2/3] sip_fake output:"
    head -10 /tmp/sip_fake_test.log
    echo ""

    # Run a simple test with caller
    echo "[3/3] Testing with caller..."

    # Start caller (will try to register and make a call)
    timeout 5s ./caller > /tmp/caller_test.log 2>&1 || true

    # Analyze results
    echo ""
    echo "Results:"
    echo "--------"

    # Check sip_fake behavior
    if grep -q "Listening on UDP port 5060" /tmp/sip_fake_test.log; then
        echo "  ✓ sip_fake listening on port 5060"
        passed_tests=$((passed_tests + 1))
    else
        echo "  ✗ sip_fake not listening"
    fi

    if [ "$scenario" == "router" ]; then
        if grep -q "Router (forward messages)" /tmp/sip_fake_test.log; then
            echo "  ✓ Router mode confirmed"
        else
            echo "  - Router mode not explicitly confirmed"
        fi
    else
        if grep -q "UAS Simulation" /tmp/sip_fake_test.log; then
            echo "  ✓ UAS simulation mode confirmed"
        else
            echo "  - UAS simulation mode not explicitly confirmed"
        fi
    fi

    # Show caller log summary
    if grep -q "Registered successfully" /tmp/caller_test.log; then
        echo "  ✓ Caller registered successfully"
    else
        echo "  - Caller registration status unknown"
    fi

    echo ""
    echo "Caller log (last 10 lines):"
    tail -10 /tmp/caller_test.log
    echo ""

    # Cleanup after test
    cleanup

    echo "Test $total_tests completed"
    echo ""
    sleep 1
done

# Summary
echo "========================================"
echo "Test Summary"
echo "========================================"
echo "Total tests: $total_tests"
echo "Passed tests: $passed_tests"
echo ""

if [ $passed_tests -eq $total_tests ]; then
    echo "✓ All tests passed!"
    exit 0
else
    echo "✗ Some tests failed"
    exit 1
fi
