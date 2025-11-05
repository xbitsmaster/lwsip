#!/bin/bash

# FreeSWITCH连接测试脚本

FREESWITCH_HOST="198.19.249.149"
SIP_PORT="5060"
RTP_PORT_START="16384"
RTP_PORT_END="16394"

echo "========================================="
echo "FreeSWITCH 连接测试"
echo "服务器: ${FREESWITCH_HOST}"
echo "========================================="
echo ""

# 测试网络连接
echo "1. 测试网络连接..."
if ping -c 3 ${FREESWITCH_HOST} > /dev/null 2>&1; then
    echo "   ✓ 网络连接正常"
else
    echo "   ✗ 无法ping通服务器"
fi
echo ""

# 测试SIP端口
echo "2. 测试SIP端口 (TCP ${SIP_PORT})..."
if timeout 3 bash -c "</dev/tcp/${FREESWITCH_HOST}/${SIP_PORT}" 2>/dev/null; then
    echo "   ✓ SIP TCP端口 ${SIP_PORT} 开放"
else
    echo "   ✗ SIP TCP端口 ${SIP_PORT} 无法连接"
fi
echo ""

# 测试SIP UDP端口（使用nc或nmap）
echo "3. 测试SIP端口 (UDP ${SIP_PORT})..."
if command -v nc &> /dev/null; then
    if echo "OPTIONS sip:${FREESWITCH_HOST} SIP/2.0" | nc -u -w 2 ${FREESWITCH_HOST} ${SIP_PORT} | grep -q "SIP"; then
        echo "   ✓ SIP UDP端口 ${SIP_PORT} 响应正常"
    else
        echo "   ! SIP UDP端口 ${SIP_PORT} 无响应（可能是防火墙或服务未启动）"
    fi
else
    echo "   ! nc命令未安装，跳过UDP测试"
fi
echo ""

# 测试示例RTP端口
echo "4. 测试RTP端口范围..."
echo "   检查端口 ${RTP_PORT_START}..."
if command -v nc &> /dev/null; then
    if timeout 2 nc -uz ${FREESWITCH_HOST} ${RTP_PORT_START} 2>/dev/null; then
        echo "   ✓ RTP端口 ${RTP_PORT_START} 可达"
    else
        echo "   ! RTP端口 ${RTP_PORT_START} 无响应"
    fi
else
    echo "   ! 需要nc命令来测试UDP端口"
fi
echo ""

# 测试SSH连接（可选）
echo "5. 测试SSH连接..."
if timeout 5 ssh -o ConnectTimeout=3 -o BatchMode=yes root@${FREESWITCH_HOST} "echo 'SSH OK'" 2>/dev/null; then
    echo "   ✓ SSH连接正常"
else
    echo "   ! SSH连接失败（可能需要配置密钥）"
fi
echo ""

echo "========================================="
echo "测试完成"
echo ""
echo "如果SIP端口测试失败，请检查："
echo "  1. FreeSWITCH容器是否正在运行"
echo "  2. 防火墙规则是否正确配置"
echo "  3. Docker端口映射是否正确"
echo ""
echo "查看FreeSWITCH状态:"
echo "  ssh root@${FREESWITCH_HOST} 'cd /opt/freeswitch && docker-compose ps'"
echo "========================================="
