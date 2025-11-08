#!/bin/bash

echo "========================================"
echo "lwsip-cli 呼叫测试"
echo "========================================"
echo "被叫方: pjsua (1000) 端口 5070"
echo "主叫方: lwsip-cli (1001)"
echo "通过: FreeSWITCH (198.19.249.149:5060)"
echo "========================================"
echo ""

# 清理之前的进程和日志
pkill -f pjsua || true
pkill -f lwsip-cli || true
mkdir -p ../build/logs
rm -f ../build/logs/lwsip_caller.log ../build/logs/lwsip_callee.log
sleep 2

# 创建被叫方保持脚本
CALLEE_SCRIPT=$(mktemp)
cat > "$CALLEE_SCRIPT" <<'EOF'
sleep 120
echo "q"
EOF

# 启动被叫方 pjsua (1000)
echo "[1/3] 启动被叫方 pjsua (1000)..."
(bash "$CALLEE_SCRIPT" | ./callee.sh > ../build/logs/lwsip_callee.log 2>&1) &
CALLEE_PID=$!
echo "      被叫方 PID: $CALLEE_PID"

# 等待被叫方注册
echo "[2/3] 等待被叫方注册 (最长15秒)..."
COUNTER=0
while [ $COUNTER -lt 15 ]; do
    if grep -q "registration success" ../build/logs/lwsip_callee.log 2>/dev/null; then
        echo "      ✓ 被叫方 1000 注册成功!"
        break
    fi
    sleep 1
    COUNTER=$((COUNTER + 1))
    if [ $((COUNTER % 3)) -eq 0 ]; then echo -n "."; fi
done
echo ""

if ! grep -q "registration success" ../build/logs/lwsip_callee.log 2>/dev/null; then
    echo "      ✗ 被叫方注册失败，退出测试"
    echo ""
    echo "被叫方日志:"
    tail -30 ../build/logs/lwsip_callee.log
    rm -f "$CALLEE_SCRIPT"
    pkill -f "pjsua.*1000" || true
    exit 1
fi

echo ""
echo "[3/3] 启动主叫方 lwsip-cli (1001 -> 1000)..."
echo "      将在注册后自动呼叫 1000，通话时间约 10 秒"
echo ""

# 运行 lwsip-cli，等待 15 秒观察
./lwsip_caller.sh > ../build/logs/lwsip_caller.log 2>&1 &
CALLER_PID=$!
echo "      主叫方 PID: $CALLER_PID"

# 等待 15 秒观察
sleep 15

# 检查主叫方进程状态
if ps -p $CALLER_PID > /dev/null 2>&1; then
    echo "      主叫方仍在运行，发送终止信号..."
    kill $CALLER_PID 2>/dev/null || true
    wait $CALLER_PID 2>/dev/null
fi
CALLER_EXIT=$?

echo ""
echo "========================================"
echo "呼叫测试完成"
echo "========================================"

# 清理
echo ""
echo "清理进程..."
rm -f "$CALLEE_SCRIPT"
pkill -f "pjsua.*1000" || true
pkill -f lwsip-cli || true

# 等待一下让进程清理完成
sleep 1

echo ""
echo "结果分析:"
echo "----------------------------------------"

# 检查被叫方日志
echo ""
echo "【被叫方 pjsua (1000)】"
if grep -q "registration success" ../build/logs/lwsip_callee.log; then
    echo "  ✓ 注册成功"
else
    echo "  ✗ 注册失败"
fi

if grep -qi "incoming.*call\|call.*from" ../build/logs/lwsip_callee.log; then
    echo "  ✓ 接收到来电"
    echo "  来电信息:"
    grep -i "call.*from\|incoming" ../build/logs/lwsip_callee.log | head -3 | sed 's/^/    /'
else
    echo "  - 未检测到来电"
fi

# 检查主叫方日志
echo ""
echo "【主叫方 lwsip-cli (1001)】"
echo "  退出码: $CALLER_EXIT"

if grep -qi "registered" ../build/logs/lwsip_caller.log; then
    echo "  ✓ 注册成功"
    grep -i "registered" ../build/logs/lwsip_caller.log | head -1 | sed 's/^/    /'
else
    echo "  - 注册状态未知"
fi

if grep -qi "calling\|ringing\|answered\|established" ../build/logs/lwsip_caller.log; then
    echo "  ✓ 呼叫状态:"
    grep -i "calling\|ringing\|answered\|established" ../build/logs/lwsip_caller.log | head -5 | sed 's/^/    /'
else
    echo "  - 未检测到呼叫信息"
fi

# 检查 session handler 输出
if grep -qi "session.*media\|audio frame\|video frame" ../build/logs/lwsip_caller.log; then
    echo "  ✓ 会话数据:"
    grep -i "session.*media\|audio frame\|video frame" ../build/logs/lwsip_caller.log | head -5 | sed 's/^/    /'
else
    echo "  - 未检测到会话数据"
fi

# 检查错误
echo ""
echo "【错误检测】"
if grep -qi "error\|failed\|timeout" ../build/logs/lwsip_caller.log; then
    echo "  ✗ 发现错误:"
    grep -i "error\|failed\|timeout" ../build/logs/lwsip_caller.log | head -5 | sed 's/^/    /'
else
    echo "  ✓ 未发现明显错误"
fi

echo ""
echo "========================================="
echo ""
echo "完整日志文件:"
echo "  被叫方: ../build/logs/lwsip_callee.log"
echo "  主叫方: ../build/logs/lwsip_caller.log"
echo ""
echo "查看完整日志:"
echo "  tail -f ../build/logs/lwsip_caller.log"
echo "  tail -f ../build/logs/lwsip_callee.log"
echo ""

