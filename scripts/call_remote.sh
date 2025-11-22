#!/bin/bash
# call_remote.sh - 使用pjsua发起SIP呼叫并录音

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MEDIA_DIR="${PROJECT_ROOT}/media"

# 使用系统pjsua
if ! command -v pjsua &> /dev/null; then
    echo "错误: 找不到pjsua命令，请先安装 pjsip"
    exit 1
fi
PJSUA="pjsua"

# 默认配置
DEFAULT_SERVER="198.19.249.149"
DEFAULT_PASSWORD="1234"
DEFAULT_LOCAL_PORT=5065
DEFAULT_DURATION=30

# 解析参数
if [ $# -lt 2 ]; then
    echo "用法: $0 <主叫号码> <被叫号码> [服务器IP] [密码] [本地端口] [通话时长秒]"
    echo "示例: $0 1000 1001 198.19.249.149 1234 5065 30"
    exit 1
fi

CALLER="$1"
CALLEE="$2"
SERVER="${3:-$DEFAULT_SERVER}"
PASSWORD="${4:-$DEFAULT_PASSWORD}"
LOCAL_PORT="${5:-$DEFAULT_LOCAL_PORT}"
DURATION="${6:-$DEFAULT_DURATION}"

# 音频文件
PLAY_FILE="${MEDIA_DIR}/test_audio_pcmu.wav"
RECORD_FILE="${MEDIA_DIR}/call_remote_record.wav"

# 清理旧录音文件
if [ -f "$RECORD_FILE" ]; then
    rm -f "$RECORD_FILE"
    echo "清理旧录音文件: $RECORD_FILE"
fi

echo "========================================"
echo "使用 pjsua 发起呼叫"
echo "  主叫: $CALLER"
echo "  被叫: $CALLEE"
echo "  服务器: $SERVER"
echo "  本地端口: $LOCAL_PORT"
echo "  播放文件: $PLAY_FILE"
echo "  录音文件: $RECORD_FILE"
echo "  通话时长: ${DURATION}秒"
echo "========================================"

echo "开始呼叫..."

# 创建命名管道用于发送命令
FIFO="/tmp/pjsua_fifo_$$"
mkfifo "$FIFO"

# 后台运行pjsua
$PJSUA --id "sip:${CALLER}@${SERVER}" \
        --registrar "sip:${SERVER}" \
        --username "${CALLER}" \
        --password "${PASSWORD}" \
        --realm "${SERVER}" \
        --local-port=${LOCAL_PORT} \
        --play-file="${PLAY_FILE}" \
        --auto-play \
        --rec-file="${RECORD_FILE}" \
        --auto-rec \
        --duration=$((DURATION + 15)) \
        --max-calls=4 \
        --snd-auto-close=0 \
        --null-audio \
        < "$FIFO" &

PJSUA_PID=$!

# 发送命令到pjsua
(
    # 等待注册完成
    sleep 5

    # 发起呼叫
    echo "m"
    echo "sip:${CALLEE}@${SERVER}"

    # 等待通话时长
    sleep ${DURATION}

    # 挂断
    echo "h"

    # 退出
    sleep 1
    echo "q"
) > "$FIFO" &

# 等待pjsua结束
wait $PJSUA_PID

# 清理命名管道
rm -f "$FIFO"

echo ""
echo "========================================"
echo "通话结束"
echo "========================================"

# 检查录音文件
if [ -f "$RECORD_FILE" ]; then
    FILE_SIZE=$(stat -f%z "$RECORD_FILE" 2>/dev/null || stat -c%s "$RECORD_FILE" 2>/dev/null)
    echo "录音文件: $RECORD_FILE"
    echo "文件大小: ${FILE_SIZE} 字节"
    if [ "$FILE_SIZE" -gt 1000 ]; then
        echo "✓ 录音成功"
    else
        echo "✗ 录音文件过小，可能录音失败"
    fi
else
    echo "✗ 未找到录音文件"
fi
