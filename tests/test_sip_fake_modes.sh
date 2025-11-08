#!/bin/bash

echo "========================================"
echo "SIP Fake Mode Testing"
echo "========================================"
echo ""

# Test results
total_tests=0
passed_tests=0

# Test function
test_mode() {
    local mode=$1
    local args=$2
    local expected=$3

    total_tests=$((total_tests + 1))

    echo "Test $total_tests: $mode"
    echo "----------------------------------------"

    # Start sip_fake in background
    ./sip_fake $args > /tmp/sip_fake_mode_test.log 2>&1 &
    local pid=$!

    # Wait for it to start
    sleep 2

    # Check if it's still running (or log exists)
    local result="FAIL"
    if [ -f /tmp/sip_fake_mode_test.log ]; then
        if grep -q "$expected" /tmp/sip_fake_mode_test.log; then
            echo "✓ PASSED: Found expected message: '$expected'"
            passed_tests=$((passed_tests + 1))
            result="PASS"
        else
            echo "✗ FAILED: Expected message not found: '$expected'"
            echo "Log content:"
            cat /tmp/sip_fake_mode_test.log
        fi
    else
        echo "✗ FAILED: No log file generated"
    fi

    # Kill the process if still running
    kill $pid 2>/dev/null || true
    pkill -f sip_fake 2>/dev/null || true
    wait $pid 2>/dev/null || true

    echo ""
    rm -f /tmp/sip_fake_mode_test.log

    return 0
}

# Run tests
test_mode "Router Mode (default)" "" "Router (forward messages)"
test_mode "UAS Simulation - Normal" "--scenario=normal" "UAS Simulation - normal (200 OK)"
test_mode "UAS Simulation - Busy" "--scenario=busy" "UAS Simulation - busy (486)"
test_mode "UAS Simulation - Reject" "--scenario=reject" "UAS Simulation - reject (603)"
test_mode "UAS Simulation - Timeout" "--scenario=timeout" "UAS Simulation - timeout (no response)"
test_mode "UAS Simulation - Unavailable" "--scenario=unavailable" "UAS Simulation - unavailable (480)"

# Summary
echo "========================================"
echo "Test Summary"
echo "========================================"
echo "Total tests: $total_tests"
echo "Passed: $passed_tests"
echo "Failed: $((total_tests - passed_tests))"
echo ""

if [ $passed_tests -eq $total_tests ]; then
    echo "✓ All tests passed!"
    exit 0
else
    echo "✗ Some tests failed"
    exit 1
fi
