#!/bin/bash
# 自动测试脚本：pjsua(1000) 作为被叫方，lwsip-cli(1001) 作为主叫方
# 用法: ./test_lwsip_call.sh

echo "========================================="
echo "测试 lwsip-cli 呼叫 pjsua"
echo "========================================="
echo "被叫方: pjsua (1000, 端口 5070)"
echo "主叫方: lwsip-cli (1001)"
echo "========================================="
echo ""

# 清理之前的进程
echo "[0/5] 清理旧进程..."
pkill -f "pjsua.*1000" 2>/dev/null || true
pkill -f "lwsip-cli" 2>/dev/null || true
sleep 2

# 清理旧日志
rm -f /tmp/lwsip_test_callee.log /tmp/lwsip_test_caller.log

# 创建被叫方保持脚本
CALLEE_SCRIPT=$(mktemp)
cat > "$CALLEE_SCRIPT" <<'EOF'
sleep 120
echo "q"
EOF

# 启动 pjsua 被叫方 (1000)
echo "[1/5] 启动 pjsua 被叫方 (1000)..."
(bash "$CALLEE_SCRIPT" | ./callee.sh > /tmp/lwsip_test_callee.log 2>&1) &
CALLEE_PID=$!
echo "      被叫方 PID: $CALLEE_PID"
echo ""

# 等待被叫方注册
echo "[2/5] 等待被叫方注册 (最长15秒)..."
COUNTER=0
REGISTERED=0
while [ $COUNTER -lt 15 ]; do
    if grep -q "registration success" /tmp/lwsip_test_callee.log 2>/dev/null; then
        echo "      ✓ 被叫方 1000 注册成功!"
        REGISTERED=1
        break
    fi
    sleep 1
    COUNTER=$((COUNTER + 1))
    echo -n "."
done
echo ""

# 检查注册结果
if [ $REGISTERED -eq 0 ]; then
    echo "      ✗ 被叫方注册失败或超时"
    echo ""
    echo "被叫方日志 (最后30行):"
    echo "----------------------------------------"
    tail -30 /tmp/lwsip_test_callee.log
    echo "----------------------------------------"
    rm -f "$CALLEE_SCRIPT"
    pkill -f "pjsua.*1000" 2>/dev/null || true
    exit 1
fi

# 检查被叫方进程
if ! ps -p $CALLEE_PID > /dev/null 2>&1; then
    echo "      ✗ 被叫方进程已退出"
    rm -f "$CALLEE_SCRIPT"
    exit 1
fi

echo ""
echo "[3/5] 启动 lwsip-cli 主叫方 (1001)..."

# 运行 lwsip-cli
./lwsip_caller.sh > /tmp/lwsip_test_caller.log 2>&1
CALLER_EXIT=$?

echo "      lwsip-cli 退出码: $CALLER_EXIT"
echo ""

echo "[4/5] 等待2秒让被叫方处理..."
sleep 2

echo ""
echo "[5/5] 测试完成！分析结果..."
echo "========================================="
echo ""

# 分析结果
echo "结果分析:"
echo "----------------------------------------"

# 被叫方分析
echo ""
echo "【被叫方 pjsua (1000)】"
if grep -q "registration success" /tmp/lwsip_test_callee.log; then
    echo "  ✓ 注册成功"
    grep "registration success" /tmp/lwsip_test_callee.log | head -1 | sed 's/^/    /'
else
    echo "  ✗ 注册失败"
fi

if grep -qi "incoming.*call\|call.*from" /tmp/lwsip_test_callee.log; then
    echo "  ✓ 接收到来电"
    grep -i "incoming.*call\|call.*from" /tmp/lwsip_test_callee.log | head -3 | sed 's/^/    /'
else
    echo "  ✗ 未接收到来电"
fi

if grep -qi "call.*connected\|200.*ok" /tmp/lwsip_test_callee.log; then
    echo "  ✓ 通话已建立"
else
    echo "  - 通话可能未建立"
fi

# 主叫方分析
echo ""
echo "【主叫方 lwsip-cli (1001)】"
echo "  退出码: $CALLER_EXIT"

# 检查 lwsip-cli 日志内容
if [ -s /tmp/lwsip_test_caller.log ]; then
    echo "  输出内容:"
    sed 's/^/    /' /tmp/lwsip_test_caller.log
else
    echo "  ⚠ 无输出（可能是问题）"
fi

# 错误检测
echo ""
echo "【错误检测】"
ERROR_FOUND=0

if grep -qi "error\|fail\|exception\|segmentation\|core dump" /tmp/lwsip_test_caller.log; then
    echo "  ✗ lwsip-cli 输出中发现错误:"
    grep -i "error\|fail\|exception\|segmentation\|core dump" /tmp/lwsip_test_caller.log | sed 's/^/    /'
    ERROR_FOUND=1
fi

if [ $CALLER_EXIT -ne 0 ]; then
    echo "  ✗ lwsip-cli 异常退出 (退出码: $CALLER_EXIT)"
    ERROR_FOUND=1
fi

if ! grep -qi "incoming.*call\|call.*from" /tmp/lwsip_test_callee.log; then
    echo "  ✗ 被叫方未收到来电 - lwsip 可能没有正确发起呼叫"
    ERROR_FOUND=1
fi

if [ $ERROR_FOUND -eq 0 ]; then
    echo "  ✓ 未发现明显错误"
fi

echo ""
echo "========================================="
echo "详细日志文件:"
echo "  被叫方: /tmp/lwsip_test_callee.log"
echo "  主叫方: /tmp/lwsip_test_caller.log"
echo "========================================="

# 清理
rm -f "$CALLEE_SCRIPT"
pkill -f "pjsua.*1000" 2>/dev/null || true

exit $ERROR_FOUND
