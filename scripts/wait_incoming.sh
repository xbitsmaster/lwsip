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

# 使用系统pjsua
if ! command -v pjsua &> /dev/null; then
    echo "错误: 找不到pjsua命令，请先安装 pjsip"
    exit 1
fi
PJSUA="pjsua"

# 运行 pjsua，添加以下参数确保正常工作：
# --duration=300: 运行5分钟（300秒）
# --max-calls=4: 最大并发呼叫数（编译时限制）
# --snd-auto-close=0: 保持音频设备打开
echo ""
echo "提示: pjsua 将运行5分钟，按 Ctrl+C 可提前停止"
echo ""

# 使用 tail -f /dev/null 来保持 stdin 打开，防止 pjsua 因读到 EOF 而退出
(tail -f /dev/null) | $PJSUA --id "sip:${USER}@${SERVER}" \
               --registrar "sip:${SERVER}" \
               --realm "*" \
               --username "${USER}" \
               --password "${PASSWORD}" \
               --local-port=${LOCAL_PORT} \
               --auto-answer=${AUTO_ANSWER} \
               --play-file="${PLAY_FILE}" \
               --auto-play \
               --rec-file="${RECORD_FILE}" \
               --auto-rec \
               --duration=300 \
               --max-calls=4 \
               --snd-auto-close=0 \
               --null-audio

echo ""
echo "========================================"
echo "pjsua 已退出"
echo "========================================"
