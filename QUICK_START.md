# lwsip v2.0 - Quick Start Guide

## 快速开始

### 1. 编译

```bash
cd /path/to/lwsip

# 编译 v2.0
mkdir -p build && cd build
cmake ..
make

# 生成的程序
./build/bin/lwsip-cli
```

### 2. 配置 FreeSWITCH

#### 添加SIP用户

编辑 `/etc/freeswitch/directory/default/1002.xml`:

```xml
<user id="1002">
  <params>
    <param name="password" value="1234"/>
    <param name="vm-password" value="1234"/>
  </params>
</user>
```

重启 FreeSWITCH:
```bash
fs_cli -x "reloadxml"
```

### 3. 运行 lwsip-cli

```bash
# 连接到 FreeSWITCH
./lwsip-cli 192.168.1.100 1002 1234

# 输出示例:
# Creating SIP client...
# Starting client...
# Registering as 1002@192.168.1.100...
# [REG] REGISTERED (code: 200)
#
# lwsip ready. Type 'help' for commands.
# >
```

### 4. 使用命令

#### 发起呼叫
```
> call sip:1001@192.168.1.100
Calling sip:1001@192.168.1.100...
[CALL] sip:1001@192.168.1.100 - CALLING
[CALL] sip:1001@192.168.1.100 - RINGING
[CALL] sip:1001@192.168.1.100 - ESTABLISHED
```

#### 接听来电
```
[INCOMING] sip:1003@192.168.1.100 -> sip:1002@192.168.1.100
Type 'answer' to accept or 'reject' to decline
> answer
Answered call
[CALL] sip:1003@192.168.1.100 - ESTABLISHED
```

#### 挂断
```
> hangup
Hung up
[CALL] - HANGUP
```

#### 拒绝来电
```
[INCOMING] sip:1003@192.168.1.100 -> sip:1002@192.168.1.100
> reject
Rejected call
```

### 5. 配置选项

#### 使用 TCP 传输
```bash
# 修改代码中的配置
config.transport_type = LWS_TRANSPORT_TCP;
```

#### 使用文件作为媒体源
```c
lws_config_t config = {
    // ... 其他配置
    .media_backend_type = LWS_MEDIA_BACKEND_FILE,
    .audio_input_file = "/path/to/audio.wav",
    .audio_output_file = "/path/to/output.wav",
};
```

#### 使用 MQTT 传输
```c
lws_config_t config = {
    // ... 其他配置
    .transport_type = LWS_TRANSPORT_MQTT,
    .mqtt_broker_host = "mqtt.example.com",
    .mqtt_broker_port = 1883,
    .mqtt_client_id = "device001",
    .mqtt_pub_topic = "sip/1002/inbox",
    .mqtt_sub_topic = "sip/1001/inbox",
};
```

## 故障排查

### 问题 1: 注册失败

**症状**: `[REG] FAILED (code: 401)`

**原因**: 用户名或密码错误

**解决**:
1. 检查 FreeSWITCH 用户配置
2. 确认用户名和密码正确
3. 检查 FreeSWITCH 日志: `fs_cli -x "console loglevel 7"`

### 问题 2: 无法连接到服务器

**症状**: `[ERROR] 0x80010003: failed to connect transport`

**原因**: 网络不通或防火墙阻止

**解决**:
1. 检查网络连接: `ping 192.168.1.100`
2. 检查防火墙: `sudo ufw status`
3. 检查 FreeSWITCH 监听: `netstat -tulpn | grep 5060`

### 问题 3: 呼叫无响应

**症状**: `[CALL] CALLING` 后无反应

**原因**: SIP 消息未正确发送或解析

**解决**:
1. 查看日志输出
2. 使用 Wireshark 抓包分析
3. 检查 SDP 是否正确生成

## API 示例

### 基本呼叫流程

```c
#include "include/lws_client.h"

// 1. 配置
lws_config_t config = {
    .server_host = "192.168.1.100",
    .server_port = 5060,
    .username = "1002",
    .password = "1234",
    .expires = 300,
    .transport_type = LWS_TRANSPORT_UDP,
    .enable_audio = 1,
    .audio_codec = LWS_AUDIO_CODEC_PCMU,
};

// 2. 回调
lws_client_handler_t handler = {
    .on_reg_state = on_reg_state,
    .on_call_state = on_call_state,
    .on_incoming_call = on_incoming_call,
    .on_error = on_error,
};

// 3. 创建客户端
lws_client_t* client = lws_client_create(&config, &handler);

// 4. 启动
lws_client_start(client);

// 5. 注册
lws_uac_register(client);

// 6. 发起呼叫
lws_session_t* session = lws_call(client, "sip:1001@192.168.1.100");

// 7. 主循环
while (running) {
    lws_client_loop(client, 100);
}

// 8. 清理
if (session) {
    lws_hangup(client, session);
}
lws_client_destroy(client);
```

### 接收呼叫

```c
// 在 on_incoming_call 回调中
static void on_incoming_call(void* param, const char* from,
                             const char* to, const char* sdp, int sdp_len)
{
    printf("Incoming call from %s\n", from);

    // 自动应答
    g_session = lws_answer(g_client, from);

    // 或者拒绝
    // lws_uas_reject(g_client, from, 486);  // Busy Here
}
```

## 下一步

- 阅读 [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) 了解实现细节
- 阅读 [TRANSPORT_DESIGN.md](TRANSPORT_DESIGN.md) 了解传输层设计
- 查看 [SUMMARY.md](SUMMARY.md) 了解完整架构

## 获取帮助

- GitHub Issues: https://github.com/your-repo/lwsip/issues
- 文档: https://lwsip.readthedocs.io

---

**注意**: v2.0 当前为开发版本，部分功能（特别是RTP媒体传输）尚未完全实现。
建议先进行 SIP 信令测试，待 RTP 功能完善后再进行媒体传输测试。
