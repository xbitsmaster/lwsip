#!/bin/bash
# 被叫方 - 1000
# 用法: ./callee.sh

PJSUA="../3rds/pjsip/pjsip-apps/bin/pjsua-aarch64-apple-darwin24.6.0"
AUDIO_FILE="../media/test_audio_pcmu.wav"

echo "========================================"
echo "被叫方 (Callee) - 1000"
echo "========================================"
echo "本地端口:  5070"
echo "音频文件:  $AUDIO_FILE"
echo "等待来电..."
echo "========================================"
echo ""

"$PJSUA" \
  --id sip:1000@198.19.249.149 \
  --registrar sip:198.19.249.149 \
  --realm "*" \
  --username 1000 \
  --password 1234 \
  --local-port 5070 \
  --play-file "$AUDIO_FILE" \
  --auto-play \
  --auto-loop \
  --auto-answer 200 \
  --max-calls 1
