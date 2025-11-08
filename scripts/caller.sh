#!/bin/bash
# 主叫方 - 1001 呼叫 1000
# 用法: ./caller.sh

PJSUA="../3rds/pjsip/pjsip-apps/bin/pjsua-aarch64-apple-darwin24.6.0"
AUDIO_FILE="../media/test_audio_pcmu.wav"

echo "========================================"
echo "主叫方 (Caller) - 1001 -> 1000"
echo "========================================"
echo "本地端口:  5080"
echo "音频文件:  $AUDIO_FILE"
echo "将在3秒后呼叫 1000..."
echo "========================================"
echo ""

# 创建临时脚本进行自动拨号
TEMP=$(mktemp)
cat > "$TEMP" <<'EOF'
sleep 3
echo "call sip:1000@198.19.249.149"
sleep 30
echo "h"
sleep 1
echo "q"
EOF

bash "$TEMP" | "$PJSUA" \
  --id sip:1001@198.19.249.149 \
  --registrar sip:198.19.249.149 \
  --realm "*" \
  --username 1001 \
  --password 1234 \
  --local-port 5080 \
  --play-file "$AUDIO_FILE" \
  --auto-play \
  --auto-loop \
  --max-calls 1

rm -f "$TEMP"
