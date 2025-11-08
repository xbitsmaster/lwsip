#!/bin/bash
# lwsip 主叫方 - 1001 呼叫 1000
# 用法: ./lwsip_caller.sh

LWSIP_CLI="../build/bin/lwsip-cli"

echo "========================================"
echo "lwsip 主叫方 (Caller) - 1001 -> 1000"
echo "========================================"
echo "SIP 服务器:  198.19.249.149:5060"
echo "本地扩展号:  1001"
echo "呼叫目标:    1000"
echo "========================================"
echo ""

# 检查lwsip-cli是否存在
if [ ! -f "$LWSIP_CLI" ]; then
    echo "错误: lwsip-cli 可执行文件不存在: $LWSIP_CLI"
    echo "请先编译项目: cd .. && cmake --build build"
    exit 1
fi

# 运行lwsip-cli
# 用法: lwsip-cli <server> <username> [password]
"$LWSIP_CLI" 198.19.249.149:5060 1001 1234
