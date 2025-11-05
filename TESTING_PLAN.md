# Lwsip 音视频通话测试方案

## 方案概述

**测试架构**:
```
┌─────────────────┐         ┌──────────────────────┐         ┌─────────────────┐
│   本地 lwsip   │◄───────►│  FreeSWITCH Server   │◄───────►│   本地 pjsua    │
│  (SIP Client)   │   SIP   │  (198.19.249.149)    │   SIP   │  (SIP Client)   │
│                 │◄───────►│                      │◄───────►│                 │
└─────────────────┘   RTP   └──────────────────────┘   RTP   └─────────────────┘
     (6001)                      Docker Container              (6002)
```

## ⚠️ 当前限制说明

**重要**: 当前 lwsip 实现状态：
- ✅ **音频**: 已完整实现 RTP 传输层，但未实现真实编解码器
- ⚠️ **视频**: 代码框架存在但未完全实现
- ⚠️ **macOS**: SDK 编译存在兼容性问题（OSSpinLock 已废弃）

**建议**:
1. **短期**: 先在 Linux 环境测试音频通话（推荐）
2. **中期**: 修复 macOS SDK 问题后在 macOS 测试
3. **长期**: 完善音视频编解码器实现

---

## 一、环境准备

### 1.1 服务器端 (198.19.249.149)

#### 步骤1: 部署 FreeSWITCH Docker

```bash
# SSH 登录到服务器
ssh user@198.19.249.149

# 创建工作目录
mkdir -p ~/freeswitch/{config,recordings}
cd ~/freeswitch

# 创建 FreeSWITCH 配置文件
cat > docker-compose.yml << 'EOF'
version: '3.8'

services:
  freeswitch:
    image: drachtio/freeswitch:latest
    container_name: freeswitch
    restart: unless-stopped
    network_mode: host
    volumes:
      - ./config:/etc/freeswitch:rw
      - ./recordings:/var/lib/freeswitch/recordings:rw
    environment:
      - SOUND_RATES=8000:16000:32000:48000
    command: freeswitch -nonat -nf
EOF

# 启动 FreeSWITCH
docker-compose up -d

# 查看日志
docker logs -f freeswitch
```

#### 步骤2: 配置 FreeSWITCH 用户

进入容器配置：

```bash
# 进入容器
docker exec -it freeswitch bash

# 编辑 SIP 配置文件
cat > /etc/freeswitch/directory/default/6001.xml << 'EOF'
<include>
  <user id="6001">
    <params>
      <param name="password" value="test123"/>
      <param name="vm-password" value="6001"/>
    </params>
    <variables>
      <variable name="toll_allow" value="domestic,international,local"/>
      <variable name="accountcode" value="6001"/>
      <variable name="user_context" value="default"/>
      <variable name="effective_caller_id_name" value="User 6001"/>
      <variable name="effective_caller_id_number" value="6001"/>
    </variables>
  </user>
</include>
EOF

# 配置第二个用户
cat > /etc/freeswitch/directory/default/6002.xml << 'EOF'
<include>
  <user id="6002">
    <params>
      <param name="password" value="test123"/>
      <param name="vm-password" value="6002"/>
    </params>
    <variables>
      <variable name="toll_allow" value="domestic,international,local"/>
      <variable name="accountcode" value="6002"/>
      <variable name="user_context" value="default"/>
      <variable name="effective_caller_id_name" value="User 6002"/>
      <variable name="effective_caller_id_number" value="6002"/>
    </variables>
  </user>
</include>
EOF

# 重载配置
fs_cli -x "reloadxml"
exit
```

#### 步骤3: 配置防火墙

```bash
# 开放 SIP 端口
sudo firewall-cmd --permanent --add-port=5060/tcp
sudo firewall-cmd --permanent --add-port=5060/udp

# 开放 RTP 端口范围
sudo firewall-cmd --permanent --add-port=16384-32768/udp

# 重载防火墙
sudo firewall-cmd --reload

# 或使用 iptables
sudo iptables -A INPUT -p udp --dport 5060 -j ACCEPT
sudo iptables -A INPUT -p tcp --dport 5060 -j ACCEPT
sudo iptables -A INPUT -p udp --dport 16384:32768 -j ACCEPT
```

#### 步骤4: 验证 FreeSWITCH 运行状态

```bash
# 进入 FreeSWITCH CLI
docker exec -it freeswitch fs_cli

# 在 fs_cli 中执行
sofia status
sofia status profile internal

# 检查注册用户
list_users

# 退出 CLI
/exit
```

---

### 1.2 本地客户端 (macOS)

#### 步骤1: 安装 PJSUA 测试工具

PJSUA 是 PJSIP 项目的命令行测试工具。

```bash
cd /Users/konghan/ClaudeSpace/lwsip/3rds

# 下载 PJSIP
wget https://github.com/pjsip/pjproject/archive/refs/tags/2.14.1.tar.gz
tar xzf 2.14.1.tar.gz
mv pjproject-2.14.1 pjsip
cd pjsip

# 配置和编译
./configure --disable-video --disable-openh264 --disable-ffmpeg
make dep
make

# 验证安装
./pjsip-apps/bin/pjsua-*-darwin --version
```

如果编译失败，可以使用 Homebrew：

```bash
# 备选方案：使用 Homebrew
brew install pjsip
which pjsua
```

#### 步骤2: 生成测试音频文件

```bash
cd /Users/konghan/ClaudeSpace/lwsip
mkdir -p test_media

# 方案A: 生成简单的测试音（推荐用于初步测试）
# 生成 10 秒 8kHz 单声道正弦波音频（PCMA 格式）
ffmpeg -f lavfi -i "sine=frequency=440:duration=10:sample_rate=8000" \
       -ac 1 -ar 8000 -acodec pcm_alaw \
       test_media/test_audio_8k.wav

# 生成 16kHz 版本
ffmpeg -f lavfi -i "sine=frequency=440:duration=10:sample_rate=16000" \
       -ac 1 -ar 16000 -acodec pcm_alaw \
       test_media/test_audio_16k.wav

# 方案B: 生成语音提示音
# 使用 say 命令（macOS）
say -o test_media/greeting.aiff "Hello, this is a test call from lwsip"
ffmpeg -i test_media/greeting.aiff -ar 8000 -ac 1 -acodec pcm_alaw \
       test_media/greeting_8k.wav

# 方案C: 下载现成的测试音频
wget https://www.kozco.com/tech/piano2.wav -O test_media/piano_orig.wav
ffmpeg -i test_media/piano_orig.wav -ar 8000 -ac 1 -acodec pcm_alaw \
       test_media/piano_8k.wav
```

#### 步骤3: 解决 macOS 编译问题

**当前问题**: SDK 中的 `sys/spinlock.h` 使用了已废弃的 `OSSpinLock`

**解决方案**:

```bash
# 修复 spinlock.h
cat > /tmp/spinlock_patch.diff << 'EOF'
--- a/3rds/sdk/include/sys/spinlock.h
+++ b/3rds/sdk/include/sys/spinlock.h
@@ -54,7 +54,11 @@

 static inline void spinlock_init(spinlock_t *locker)
 {
+#if defined(__APPLE__)
+    *locker = OS_UNFAIR_LOCK_INIT;
+#else
     *locker = 0;
+#endif
 }

 static inline void spinlock_destroy(spinlock_t *locker)
@@ -91,7 +95,11 @@ static inline void spinlock_destroy(spinlock_t *locker)

 static inline void spinlock_lock(spinlock_t *locker)
 {
+#if defined(__APPLE__)
+    os_unfair_lock_lock(locker);
+#else
     OSSpinLockLock(locker);
+#endif
 }

 static inline void spinlock_unlock(spinlock_t *locker)
 {
+#if defined(__APPLE__)
+    os_unfair_lock_unlock(locker);
+#else
     OSSpinLockUnlock(locker);
+#endif
 }

 static inline int spinlock_trylock(spinlock_t *locker)
 {
+#if defined(__APPLE__)
+    return os_unfair_lock_trylock(locker) ? 1 : 0;
+#else
     return OSSpinLockTry(locker) ? 1 : 0;
+#endif
 }
EOF

# 应用补丁
cd 3rds/sdk
patch -p1 < /tmp/spinlock_patch.diff
```

或者直接编辑文件：

```bash
# 编辑 3rds/sdk/include/sys/spinlock.h
# 在文件开头添加 macOS 特定的头文件
```

#### 步骤4: 编译 lwsip

```bash
cd /Users/konghan/ClaudeSpace/lwsip

# 清理之前的构建
./clean.sh

# 构建依赖库
make deps

# 如果 SDK 编译失败，需要先修复上述 spinlock 问题
# 修复后重新编译
cd 3rds/sdk && make clean && make
cd ../..

# 构建 lwsip
make lwsip

# 或直接使用 build.sh
./build.sh Release
```

**如果 macOS 编译仍然失败，建议使用 Linux 虚拟机或 Docker**:

```bash
# 使用 Docker 编译（备选方案）
docker run -it --rm -v $(pwd):/workspace -w /workspace ubuntu:22.04 bash

# 在容器内
apt-get update
apt-get install -y build-essential cmake git
make deps
make lwsip
```

---

## 二、测试执行

### 2.1 PJSUA 连接测试

#### 测试1: PJSUA 注册到 FreeSWITCH

```bash
# 启动 pjsua 作为用户 6002
pjsua --id sip:6002@198.19.249.149 \
      --registrar sip:198.19.249.149 \
      --realm "*" \
      --username 6002 \
      --password test123 \
      --null-audio \
      --auto-answer 200

# 预期输出:
# Registration successful
# Ready for incoming calls
```

**PJSUA 常用命令**:
```
m                    # 拨打电话
h                    # 挂断
a                    # 应答来电
+/-                  # 调节音量
q                    # 退出
```

#### 测试2: PJSUA 播放测试音频

```bash
# 使用 wav 文件作为音频源
pjsua --id sip:6002@198.19.249.149 \
      --registrar sip:198.19.249.149 \
      --realm "*" \
      --username 6002 \
      --password test123 \
      --play-file /Users/konghan/ClaudeSpace/lwsip/test_media/test_audio_8k.wav \
      --auto-loop
```

### 2.2 Lwsip 连接测试

#### 测试1: Lwsip 注册到 FreeSWITCH

```bash
cd /Users/konghan/ClaudeSpace/lwsip

# 启动 lwsip 作为用户 6001
./build/bin/lwsip -s 198.19.249.149 -p 5060 -u 6001 -w test123

# 预期输出:
# Registration state: REGISTERING
# Registration state: REGISTERED (200 OK)
```

**在 FreeSWITCH 上验证**:
```bash
# 进入 FreeSWITCH CLI
docker exec -it freeswitch fs_cli

# 查看注册用户
sofia status profile internal reg
# 应该看到 6001 已注册
```

#### 测试2: Lwsip 呼出测试

在 lwsip 交互界面输入：
```
call sip:6002@198.19.249.149
```

**预期流程**:
1. Lwsip 发送 INVITE
2. PJSUA 收到来电并自动应答（如果启用了 --auto-answer）
3. Lwsip 显示: CALLING -> RINGING -> ANSWERED
4. 双方开始 RTP 媒体流传输

**监控 RTP 流量**:
```bash
# 在 lwsip 机器上
sudo tcpdump -i any -n udp portrange 10000-20000 -c 100

# 应该看到 RTP 包交换
```

#### 测试3: Lwsip 呼入测试

**步骤**:
1. 启动 lwsip（用户 6001）
2. 在 PJSUA 中拨打：
   ```
   m
   sip:6001@198.19.249.149
   ```
3. Lwsip 显示来电通知
4. 在 lwsip 中输入 `answer` 应答

### 2.3 抓包分析

#### SIP 消息抓包

```bash
# 抓取 SIP 信令
sudo tcpdump -i any -n -s 0 -A port 5060 -w sip_trace.pcap

# 使用 Wireshark 分析
wireshark sip_trace.pcap

# 或使用 sngrep（更友好）
sudo sngrep -d any port 5060
```

#### RTP 媒体流抓包

```bash
# 抓取 RTP 包
sudo tcpdump -i any -n udp portrange 16384-32768 -w rtp_trace.pcap

# 在 Wireshark 中:
# 1. 打开 rtp_trace.pcap
# 2. Telephony -> RTP -> RTP Streams
# 3. 选择流 -> Analyze -> Player
# 4. 可以播放音频（如果有有效的编码数据）
```

---

## 三、验证清单

### 3.1 FreeSWITCH 服务端

- [ ] Docker 容器运行正常
- [ ] SIP 端口 5060 可访问
- [ ] RTP 端口范围开放
- [ ] 用户 6001/6002 配置正确
- [ ] 可以通过 fs_cli 连接

### 3.2 PJSUA 客户端

- [ ] 成功编译/安装
- [ ] 可以注册到 FreeSWITCH
- [ ] 可以拨打/接听电话
- [ ] 可以播放测试音频文件

### 3.3 Lwsip 客户端

- [ ] 所有源文件编译成功
- [ ] 可执行文件生成
- [ ] 可以注册到 FreeSWITCH
- [ ] 可以发起呼叫
- [ ] 可以接收呼叫
- [ ] 可以发送/接收 RTP 包

### 3.4 音视频测试

- [ ] 测试音频文件生成
- [ ] RTP 音频包格式正确
- [ ] Payload Type 正确（8=PCMA）
- [ ] 包序号和时间戳递增
- [ ] 抓包可见双向 RTP 流

---

## 四、已知问题和解决方案

### 问题1: macOS SDK 编译失败

**症状**: `OSSpinLock` 未声明

**解决方案**:
- 应用上述 spinlock 补丁
- 或在 Linux 环境编译

### 问题2: Lwsip 注册失败

**可能原因**:
- 网络不通：`ping 198.19.249.149`
- 端口未开放：`telnet 198.19.249.149 5060`
- 认证信息错误：检查用户名密码

**调试方法**:
```bash
# 抓包查看 SIP 消息
sudo tcpdump -i any -n -A port 5060
```

### 问题3: 无 RTP 流量

**可能原因**:
- 防火墙阻止 RTP 端口
- NAT 问题
- SDP 协商失败

**检查方法**:
```bash
# 查看本地 RTP 端口绑定
netstat -an | grep 10000

# 测试 UDP 连通性
nc -u 198.19.249.149 16384
```

### 问题4: 音频无输出

**当前限制**: Lwsip 的音频回调只打印日志，未实现真实播放

**临时验证方法**:
- 检查控制台是否有 "Recv audio" 日志
- 使用 Wireshark 播放 RTP 流
- 抓包后使用 sox 转换 RTP 数据

---

## 五、测试脚本

### 自动化测试脚本

```bash
#!/bin/bash
# test_lwsip.sh - 自动化测试脚本

FREESWITCH_IP="198.19.249.149"
FREESWITCH_PORT="5060"

echo "=== Lwsip Automated Test Suite ==="

# 1. 检查 FreeSWITCH 可达性
echo -n "Checking FreeSWITCH connectivity... "
if nc -z -w 2 $FREESWITCH_IP $FREESWITCH_PORT; then
    echo "✓ OK"
else
    echo "✗ FAILED"
    exit 1
fi

# 2. 检查可执行文件
echo -n "Checking lwsip binary... "
if [ -x "./build/bin/lwsip" ]; then
    echo "✓ OK"
else
    echo "✗ FAILED (not found or not executable)"
    exit 1
fi

# 3. 启动 PJSUA
echo "Starting PJSUA (user 6002)..."
pjsua --id sip:6002@$FREESWITCH_IP \
      --registrar sip:$FREESWITCH_IP \
      --username 6002 --password test123 \
      --null-audio --auto-answer 200 > pjsua.log 2>&1 &
PJSUA_PID=$!
sleep 3

# 4. 启动 Lwsip
echo "Starting Lwsip (user 6001)..."
./build/bin/lwsip -s $FREESWITCH_IP -p $FREESWITCH_PORT \
                   -u 6001 -w test123 > lwsip.log 2>&1 &
LWS_PID=$!
sleep 3

# 5. 检查进程
if ps -p $LWS_PID > /dev/null && ps -p $PJSUA_PID > /dev/null; then
    echo "✓ Both clients started successfully"
else
    echo "✗ Client startup failed"
    kill $LWS_PID $PJSUA_PID 2>/dev/null
    exit 1
fi

# 6. 等待测试
echo ""
echo "Test environment ready!"
echo "- PJSUA PID: $PJSUA_PID (user 6002)"
echo "- Lwsip PID: $LWS_PID (user 6001)"
echo ""
echo "Press Enter to stop all clients..."
read

# 7. 清理
kill $LWS_PID $PJSUA_PID
echo "Test completed"
```

---

## 六、后续改进建议

### 短期改进（1-2 周）

1. **修复 macOS 编译问题**
   - 完成 spinlock 补丁
   - 测试 SDK 在 macOS 上的完整编译

2. **完善音频实现**
   - 集成 PortAudio 进行音频播放
   - 实现麦克风输入
   - 添加音频文件播放功能

3. **增强调试能力**
   - 添加详细日志级别控制
   - 实现 SIP 消息保存
   - RTP 统计信息输出

### 中期改进（1-2 月）

1. **真实编解码器**
   - 集成 libopus 或 speex
   - 实现 PCMA/PCMU 编解码
   - 支持多种采样率

2. **视频支持**
   - 完善视频 RTP 发送
   - 集成 FFmpeg 进行编解码
   - H.264 支持

3. **稳定性增强**
   - 添加异常处理
   - 内存泄漏检测
   - 压力测试

### 长期改进（3+ 月）

1. **高级特性**
   - SRTP 加密
   - DTMF RFC 2833
   - 呼叫转移（REFER）
   - 三方通话

2. **NAT 穿透**
   - STUN 客户端
   - TURN 支持
   - ICE 实现

3. **GUI 客户端**
   - 图形界面
   - 联系人管理
   - 通话历史

---

## 附录

### A. FreeSWITCH 常用命令

```bash
# 进入 CLI
docker exec -it freeswitch fs_cli

# 查看注册用户
sofia status profile internal reg

# 查看活动通话
show calls

# 查看通道
show channels

# 重载配置
reloadxml

# 查看日志级别
console loglevel

# 设置日志级别
console loglevel debug
```

### B. PJSUA 参数说明

```bash
--id <url>              # SIP URI
--registrar <url>       # 注册服务器
--username <name>       # 用户名
--password <pass>       # 密码
--realm <realm>         # 认证域（"*" 表示任意）
--null-audio            # 使用空音频设备（测试用）
--play-file <file>      # 播放文件
--rec-file <file>       # 录音
--auto-answer <code>    # 自动应答（200=接听）
--duration <sec>        # 通话持续时间
--no-tcp                # 禁用 TCP
--use-srtp <n>          # SRTP 模式（0=禁用）
```

### C. 网络诊断命令

```bash
# 测试 SIP 端口
nc -zv 198.19.249.149 5060

# 测试 UDP 端口
nc -zuv 198.19.249.149 16384

# 路由跟踪
traceroute 198.19.249.149

# 持续 ping
ping -i 0.5 198.19.249.149

# 查看本地监听端口
netstat -an | grep LISTEN
```

---

## 总结

此方案提供了从服务器部署到客户端测试的完整流程。关键成功因素：

1. ✅ 网络连通性（防火墙配置）
2. ✅ FreeSWITCH 正确配置
3. ⚠️ macOS SDK 编译问题（需要修复）
4. ✅ 测试环境搭建（PJSUA + 测试音频）
5. ✅ 抓包分析能力（验证协议正确性）

建议先在 **Linux 环境**完成初步测试，验证 lwsip 的核心功能正确后，再解决 macOS 平台的兼容性问题。
