#!/bin/bash

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# 默认配置
DEFAULT_SERVER="198.19.249.149"
DEFAULT_PASSWORD="1234"
DEFAULT_LOCAL_PORT=5066
DEFAULT_AUTO_ANSWER=200

# 音频文件配置
MEDIA_DIR="${PROJECT_DIR}/media"
PLAY_FILE="${MEDIA_DIR}/test_audio_pcmu.wav"
RECORD_FILE="${MEDIA_DIR}/wait_record.wav"

# 显示使用说明
usage() {
    echo "Usage: $0 <user> [server] [password] [local_port] [auto_answer]"
    echo ""
    echo "参数说明:"
    echo "  user         用户号码 (必填)"
    echo "  server       SIP服务器地址 (可选，默认: $DEFAULT_SERVER)"
    echo "  password     用户密码 (可选，默认: $DEFAULT_PASSWORD)"
    echo "  local_port   本地端口 (可选，默认: $DEFAULT_LOCAL_PORT)"
    echo "  auto_answer  自动应答代码 (可选，默认: $DEFAULT_AUTO_ANSWER, 0表示手动应答)"
    echo ""
    echo "音频配置:"
    echo "  播放文件: ${PLAY_FILE}"
    echo "  录音文件: ${RECORD_FILE}"
    echo ""
    echo "示例:"
    echo "  $0 1000"
    echo "  $0 1001 198.19.249.149"
    echo "  $0 1002 198.19.249.149 5678"
    echo "  $0 1003 198.19.249.149 1234 5061"
    echo "  $0 1000 198.19.249.149 1234 5060 0    # 手动应答"
    exit 1
}

# 检查参数数量
if [ $# -lt 1 ]; then
    echo "错误: 缺少必要参数"
    echo ""
    usage
fi

# 解析参数
USER=$1
SERVER=${2:-$DEFAULT_SERVER}
PASSWORD=${3:-$DEFAULT_PASSWORD}
LOCAL_PORT=${4:-$DEFAULT_LOCAL_PORT}
AUTO_ANSWER=${5:-$DEFAULT_AUTO_ANSWER}

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

# 显示注册信息
echo "========================================"
echo "正在注册到 SIP 服务器，等待来电..."
echo "  用户: $USER"
echo "  服务器: $SERVER"
echo "  本地端口: $LOCAL_PORT"
if [ "$AUTO_ANSWER" = "0" ]; then
    echo "  应答模式: 手动应答"
else
    echo "  应答模式: 自动应答 (${AUTO_ANSWER})"
fi
echo "  播放文件: $PLAY_FILE"
echo "  录音文件: $RECORD_FILE"
echo "========================================"

# pjsua 可执行文件路径
PJSUA="${PROJECT_DIR}/3rds/pjsip/pjsip-apps/bin/pjsua-aarch64-apple-darwin24.6.0"

# 检查 pjsua 是否存在
if [ ! -f "$PJSUA" ]; then
    echo "错误: pjsua 不存在: $PJSUA"
    exit 1
fi

# 执行注册（使用 expect 保持运行）
# 如果没有 expect，使用输入重定向让 pjsua 等待
if command -v expect &> /dev/null; then
    # 使用 expect 保持 pjsua 运行
    expect <<EOF
spawn "$PJSUA" --id "sip:${USER}@${SERVER}" \\
               --registrar "sip:${SERVER}" \\
               --realm "*" \\
               --username "${USER}" \\
               --password "${PASSWORD}" \\
               --local-port=${LOCAL_PORT} \\
               --auto-answer=${AUTO_ANSWER} \\
               --play-file="${PLAY_FILE}" \\
               --auto-play \\
               --rec-file="${RECORD_FILE}" \\
               --auto-rec
interact
EOF
else
    # 使用输入重定向保持运行（等待用户按 Ctrl+C）
    echo "等待来电... 按 Ctrl+C 退出"
    cat | "$PJSUA" --id "sip:${USER}@${SERVER}" \
                   --registrar "sip:${SERVER}" \
                   --realm "*" \
                   --username "${USER}" \
                   --password "${PASSWORD}" \
                   --local-port=${LOCAL_PORT} \
                   --auto-answer=${AUTO_ANSWER} \
                   --play-file="${PLAY_FILE}" \
                   --auto-play \
                   --rec-file="${RECORD_FILE}" \
                   --auto-rec
fi
