#!/bin/bash
# lwsip 主叫方 - 1001 呼叫 1000
# 用法: ./lwsip_caller.sh

LWSIP_CLI="../build/bin/lwsip-cli"
AUDIO_FILE="../media/test_audio_pcmu.wav"

echo "========================================"
echo "lwsip 主叫方 (Caller) - 1001 -> 1000"
echo "========================================"
echo "SIP 服务器:  198.19.249.149:5060"
echo "本地扩展号:  1001"
echo "呼叫目标:    sip:1000@198.19.249.149"
echo "音频文件:    $AUDIO_FILE"
echo "========================================"
echo ""

# 检查lwsip-cli是否存在
if [ ! -f "$LWSIP_CLI" ]; then
    echo "错误: lwsip-cli 可执行文件不存在: $LWSIP_CLI"
    echo "请先编译项目: cd .. && cmake --build build"
    exit 1
fi

# 检查音频文件是否存在
if [ ! -f "$AUDIO_FILE" ]; then
    echo "警告: 音频文件不存在: $AUDIO_FILE"
    echo "将不播放音频文件"
    AUDIO_PARAM=""
else
    AUDIO_PARAM="-f $AUDIO_FILE"
fi

# 运行lwsip-cli，自动呼叫 sip:1000@198.19.249.149
# 参数说明:
#   -c: 自动呼叫的 URI
#   -f: 播放的音频文件
"$LWSIP_CLI" $AUDIO_PARAM -c "sip:1000@198.19.249.149" 198.19.249.149:5060 1001 1234
