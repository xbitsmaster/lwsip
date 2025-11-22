#!/bin/bash

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# 默认配置
DEFAULT_SERVER="198.19.249.149"
DEFAULT_PASSWORD="1234"
DEFAULT_LOCAL_PORT=5060

# 音频文件配置
MEDIA_DIR="${PROJECT_DIR}/media"
PLAY_FILE="${MEDIA_DIR}/test_audio_pcmu.wav"
RECORD_FILE="${MEDIA_DIR}/call_record.wav"

# 显示使用说明
usage() {
    echo "Usage: $0 <caller> <callee> [server] [password]"
    echo ""
    echo "参数说明:"
    echo "  caller      主叫号码 (必填)"
    echo "  callee      被叫号码 (必填)"
    echo "  server      SIP服务器地址 (可选，默认: $DEFAULT_SERVER)"
    echo "  password    主叫密码 (可选，默认: $DEFAULT_PASSWORD)"
    echo ""
    echo "音频配置:"
    echo "  播放文件: ${PLAY_FILE}"
    echo "  录音文件: ${RECORD_FILE}"
    echo ""
    echo "示例:"
    echo "  $0 1000 1001"
    echo "  $0 1000 1002 198.19.249.149"
    echo "  $0 1000 1003 198.19.249.149 5678"
    exit 1
}

# 检查参数数量
if [ $# -lt 2 ]; then
    echo "错误: 缺少必要参数"
    echo ""
    usage
fi

# 解析参数
CALLER=$1
CALLEE=$2
SERVER=${3:-$DEFAULT_SERVER}
PASSWORD=${4:-$DEFAULT_PASSWORD}

# 检查播放文件是否存在
if [ ! -f "$PLAY_FILE" ]; then
    echo "错误: 播放文件不存在: $PLAY_FILE"
    exit 1
fi

# 确保 media 目录存在
mkdir -p "$MEDIA_DIR"

# 清理旧的录音文件
if [ -f "$RECORD_FILE" ]; then
    echo "清理旧的录音文件: $RECORD_FILE"
    rm -f "$RECORD_FILE"
fi

# 显示呼叫信息
echo "========================================"
echo "正在发起呼叫..."
echo "  主叫: $CALLER"
echo "  被叫: $CALLEE"
echo "  服务器: $SERVER"
echo "  播放文件: $PLAY_FILE"
echo "  录音文件: $RECORD_FILE"
echo "========================================"

# 执行呼叫
pjsua --id "sip:${CALLER}@${SERVER}" \
      --registrar "sip:${SERVER}" \
      --realm "*" \
      --username "${CALLER}" \
      --password "${PASSWORD}" \
      --local-port=${DEFAULT_LOCAL_PORT} \
      --duration=15 \
      --play-file="${PLAY_FILE}" \
      --auto-play \
      --rec-file="${RECORD_FILE}" \
      --auto-rec \
      "sip:${CALLEE}@${SERVER}"
