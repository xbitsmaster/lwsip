# lwsip-cli - SIP Command Line Client

lwsip 项目的命令行 SIP 客户端工具，用于快速测试 SIP 呼叫功能。

## 功能特性

- ✅ SIP 注册到服务器
- ✅ 发起音频呼叫（UAC）
- ✅ 接收音频呼叫（UAS）
- ✅ 音频文件播放（支持 .wav 和 .mp4）
- ✅ 实时音频采集（麦克风）
- ✅ ICE 候选收集
- ✅ RTP 媒体会话
- ✅ 自动格式检测

## 构建

lwsip-cli 作为主项目的一部分自动构建：

```bash
# 在 lwsip 根目录
cd build
cmake ..
make lwsip-cli

# 构建产物
# - 可执行文件: build/bin/lwsip-cli
```

## 使用方法

### 命令行语法

```bash
./build/bin/lwsip-cli [OPTIONS]
```

### 必需参数

| 参数 | 长参数 | 说明 | 示例 |
|------|--------|------|------|
| `-s` | `--server` | SIP 服务器地址 | `sip:192.168.1.100:5060` |
| `-u` | `--username` | SIP 用户名 | `1001` |
| `-p` | `--password` | SIP 密码 | `secret` |

### 可选参数

| 参数 | 长参数 | 说明 | 示例 |
|------|--------|------|------|
| `-d` | `--device` | 音频文件路径 (.wav 或 .mp4) | `audio.wav` |
| `-c` | `--call` | 呼叫目标用户 | `1002` |
| `-h` | `--help` | 显示帮助信息 | - |

## 使用场景

### 场景 1：等待来电（UAS 模式）

使用真实麦克风作为音频源：

```bash
./build/bin/lwsip-cli \
    -s sip:192.168.1.100:5060 \
    -u 1001 \
    -p secret
```

**输出示例**：
```
===========================================
  lwsip CLI - SIP Client v1.0
===========================================

[CLI] Transport created on port 35420
[CLI] Audio capture device opened
[CLI] Audio playback device opened
[CLI] Media session created
[CLI] SIP agent created
[CLI] ✓ Registered successfully
[CLI] Waiting for incoming calls (press Ctrl+C to quit)...
```

### 场景 2：主动发起呼叫（UAC 模式）

```bash
./build/bin/lwsip-cli \
    -s sip:192.168.1.100:5060 \
    -u 1001 \
    -p secret \
    -c 1002
```

**输出示例**：
```
===========================================
  lwsip CLI - SIP Client v1.0
===========================================

[CLI] Transport created on port 35421
[CLI] Audio capture device opened
[CLI] Audio playback device opened
[CLI] Media session created
[CLI] SIP agent created
[CLI] ✓ Registered successfully
[CLI] Making call to: 1002
[CLI] Call connected!
```

### 场景 3：使用音频文件

使用 WAV 文件作为音频源：

```bash
./build/bin/lwsip-cli \
    -s sip:192.168.1.100:5060 \
    -u 1001 \
    -p secret \
    -d audio.wav \
    -c 1002
```

**输出示例**：
```
[CLI] Detected WAV file format (PCM 16-bit)
[CLI] Audio capture device opened
[CLI] Making call to: 1002
```

使用 MP4 文件：

```bash
./build/bin/lwsip-cli \
    -s sip:192.168.1.100:5060 \
    -u 1001 \
    -p secret \
    -d audio.mp4 \
    -c 1002
```

**输出示例**：
```
[CLI] Detected MP4 file format
[CLI] Audio capture device opened
[CLI] Making call to: 1002
```

## 默认配置

lwsip-cli 使用以下默认配置：

- **传输协议**: UDP
- **本地端口**: 自动分配
- **注册过期时间**: 3600 秒
- **音频编码**: PCMA (G.711 A-law)
- **音频采样率**: 8000 Hz
- **音频声道**: 单声道 (Mono)
- **帧时长**: 20ms

## 支持的音频格式

### WAV 文件
- **编码**: PCM 16-bit Little Endian
- **采样率**: 8000 Hz（可由文件头指定）
- **声道**: 单声道
- **文件扩展名**: `.wav`

### MP4 文件
- **编码**: PCMA (G.711 A-law)
- **采样率**: 8000 Hz
- **声道**: 单声道
- **文件扩展名**: `.mp4`, `.m4a`

## 完整示例

### 双向呼叫测试

**终端 1 - 被叫方（1000）**：
```bash
./build/bin/lwsip-cli \
    -s sip:192.168.1.100:5060 \
    -u 1000 \
    -p 1000
```

**终端 2 - 主叫方（1001）**：
```bash
./build/bin/lwsip-cli \
    -s sip:192.168.1.100:5060 \
    -u 1001 \
    -p 1001 \
    -c 1000
```

### 使用文件进行回音测试

```bash
./build/bin/lwsip-cli \
    -s sip:192.168.1.100:5060 \
    -u 1001 \
    -p secret \
    -d test.wav \
    -c echo
```

（假设 SIP 服务器有 `echo` 回音测试扩展）

## 工作流程

1. **初始化**
   - 初始化 lwsip 定时器系统
   - 创建 UDP 传输层
   - 创建音频捕获和播放设备
   - 创建媒体会话
   - 创建 SIP 代理

2. **注册**
   - 解析服务器地址
   - 发送 REGISTER 请求
   - 等待注册成功

3. **呼叫处理**
   - **UAC 模式**：注册成功后自动呼叫目标用户
   - **UAS 模式**：等待来电并自动接听

4. **媒体会话**
   - ICE 候选收集
   - SDP 协商
   - 建立 RTP 会话
   - 开始音频传输

5. **退出**
   - 清理媒体会话
   - 注销 SIP
   - 释放所有资源

## 退出方式

使用以下方式退出程序：

1. 按 `Ctrl+C` 发送中断信号
2. 发送 `SIGTERM` 信号

退出时会自动：
- 挂断活动呼叫
- 注销 SIP 账号
- 清理所有资源

## 状态回调输出

### 注册状态

```
[CLI] ✓ Registered successfully
[CLI] ✗ Registration failed
```

### 呼叫状态

```
[CLI] Making call to: 1002
[CLI] Call connected!
[CLI] Call terminated
```

### 来电通知

```
[CLI] Incoming call from: 1001@192.168.1.100
[CLI] Auto-answering call
```

### 媒体会话

```
[CLI] Media session created
[CLI] Media session error: -1 - Connection failed
```

## 调试

### 启用调试日志

修改代码中的日志级别或使用系统工具：

```bash
# 抓取 SIP 消息
sudo tcpdump -i any -n port 5060 -A -w sip.pcap

# 抓取 RTP 流
sudo tcpdump -i any -n portrange 10000-20000 -w rtp.pcap
```

### 使用 Wireshark 分析

```bash
wireshark sip.pcap
```

查看 SIP 信令和 RTP 媒体流。

## 与 SIP 服务器互通

### Asterisk

配置 `sip.conf`:
```ini
[1001]
type=friend
context=default
host=dynamic
secret=secret
disallow=all
allow=pcma
```

### FreeSWITCH

配置用户：
```xml
<user id="1001">
  <params>
    <param name="password" value="secret"/>
  </params>
  <variables>
    <variable name="accountcode" value="1001"/>
  </variables>
</user>
```

## 限制

当前版本的限制：

- 单个并发呼叫
- 仅支持音频（无视频）
- 仅支持 UDP 传输
- 不支持 TLS 加密
- 音频编码固定为 PCMA
- WAV 文件格式支持有限

这些限制将在未来版本中改进。

## 故障排除

### 注册失败

**症状**：
```
[CLI] ✗ Registration failed
```

**解决方法**：
1. 检查服务器地址和端口
2. 验证用户名和密码
3. 确认网络连接
4. 检查防火墙设置

### 音频设备打开失败

**症状**：
```
[CLI] Failed to create audio capture device
```

**解决方法**：
1. 检查音频文件是否存在
2. 验证文件格式（.wav 或 .mp4）
3. 确认文件权限
4. 如果使用麦克风，检查设备权限

### 无法建立呼叫

**症状**：
```
[CLI] Failed to make call
```

**解决方法**：
1. 确认已成功注册
2. 检查目标用户是否存在
3. 验证对方是否在线
4. 查看 SIP 服务器日志

## 源代码

CLI 工具源代码位于 `cmd/lwsip-cli.c`。

主要功能模块：
- 命令行参数解析（getopt）
- lwsip 组件初始化
- SIP 代理事件回调
- 媒体会话回调
- 信号处理
- 主事件循环

## 开发和扩展

### 添加新的音频格式

修改 `detect_audio_file_format()` 函数：

```c
else if (strcmp(ext_lower, "ogg") == 0) {
    *format = LWS_AUDIO_FMT_OPUS;
    *sample_rate = 48000;
    *channels = 2;
    return 0;
}
```

### 添加命令行选项

在 `parse_args()` 函数中添加新选项：

```c
static struct option long_options[] = {
    {"server", required_argument, 0, 's'},
    {"username", required_argument, 0, 'u'},
    {"password", required_argument, 0, 'p'},
    {"device", required_argument, 0, 'd'},
    {"call", required_argument, 0, 'c'},
    {"codec", required_argument, 0, 'C'},  // 新选项
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
};
```

## 版本信息

- **Version**: 1.0
- **Last Updated**: 2025-11-08
- **Status**: Stable

## 许可证

与主项目 lwsip 保持一致。

---

更多信息请参考 [../README.md](../README.md)
