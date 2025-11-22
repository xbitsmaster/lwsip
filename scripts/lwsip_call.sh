#!/bin/bash

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# 默认配置（使用 call_remote.sh 中的配置）
DEFAULT_SERVER="198.19.249.149"
DEFAULT_PASSWORD="1234"
DEFAULT_CALLER="1000"
DEFAULT_CALLEE="1001"

# 音频文件配置
MEDIA_DIR="${PROJECT_DIR}/media"
PLAY_FILE="${MEDIA_DIR}/test_video.mp4"      # MP4文件（只使用音频轨道）
RECORD_FILE="${MEDIA_DIR}/lwsip_record.mp4"  # MP4输出（只写入音频）

# lwsip-cli 可执行文件路径
LWSIP_CLI="${PROJECT_DIR}/build/bin/lwsip-cli"

# 显示使用说明
usage() {
    echo "Usage: $0 [caller] [callee] [server] [password]"
    echo ""
    echo "参数说明:"
    echo "  caller      主叫号码 (可选，默认: $DEFAULT_CALLER)"
    echo "  callee      被叫号码 (可选，默认: $DEFAULT_CALLEE)"
    echo "  server      SIP服务器地址 (可选，默认: $DEFAULT_SERVER)"
    echo "  password    主叫密码 (可选，默认: $DEFAULT_PASSWORD)"
    echo ""
    echo "音频配置:"
    echo "  播放文件: ${PLAY_FILE}"
    echo "  录音文件: ${RECORD_FILE}"
    echo ""
    echo "示例:"
    echo "  $0                    # 使用默认配置"
    echo "  $0 1000 1001          # 指定主被叫"
    echo "  $0 1000 1002 198.19.249.149"
    echo "  $0 1000 1003 198.19.249.149 5678"
    exit 1
}

# 检查是否需要显示帮助
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage
fi

# 检查 lwsip-cli 是否存在
if [ ! -f "$LWSIP_CLI" ]; then
    echo "错误: lwsip-cli 可执行文件不存在: $LWSIP_CLI"
    echo "请先编译项目: cd build && make lwsip-cli"
    exit 1
fi

# 解析参数
CALLER=${1:-$DEFAULT_CALLER}
CALLEE=${2:-$DEFAULT_CALLEE}
SERVER=${3:-$DEFAULT_SERVER}
PASSWORD=${4:-$DEFAULT_PASSWORD}

# 检查播放文件是否存在
if [ ! -f "$PLAY_FILE" ]; then
    echo "错误: 播放文件不存在: $PLAY_FILE"
    exit 1
fi

# 确保 media 目录存在
mkdir -p "$MEDIA_DIR"

# 显示呼叫信息
echo "========================================"
echo "使用 lwsip-cli 发起呼叫"
echo "  主叫: $CALLER"
echo "  被叫: $CALLEE"
echo "  服务器: $SERVER"
echo "  播放文件: $PLAY_FILE"
echo "  录音文件: $RECORD_FILE"
echo "========================================"
echo ""

# 执行呼叫
"$LWSIP_CLI" \
    -s "sip:${SERVER}:5060" \
    -u "$CALLER" \
    -p "$PASSWORD" \
    -d "$PLAY_FILE" \
    -r "$RECORD_FILE" \
    -c "$CALLEE"

echo ""
echo "========================================"
echo "通话结束"
echo "========================================"
