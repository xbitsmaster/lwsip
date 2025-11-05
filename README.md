# lwsip v2.0 - Clean Architecture

## 概述

v2.0是lwsip的全新架构设计，基于对media-server库（libsip、librtp、librtsp）的深入分析，采用清晰的三层架构：

```
┌─────────────────────────────────────────┐
│         Application Layer               │
│  (User Code: main.c, CLI, GUI, etc.)    │
└─────────────────────────────────────────┘
                  ↓
┌─────────────────────────────────────────┐
│        lws_client (SIP Layer)           │
│  ┌──────────┐         ┌──────────┐     │
│  │ lws_uac  │         │ lws_uas  │     │
│  │ (Client) │         │ (Server) │     │
│  └──────────┘         └──────────┘     │
│    REGISTER/INVITE      INVITE/BYE     │
└─────────────────────────────────────────┘
                  ↓
┌─────────────────────────────────────────┐
│      lws_session (RTP Layer)            │
│  ┌─────────────┐   ┌─────────────┐     │
│  │lws_payload  │   │  RTP/RTCP   │     │
│  │  Encoder    │   │   Session   │     │
│  └─────────────┘   └─────────────┘     │
│   H.264/PCMU       Statistics/Reports  │
└─────────────────────────────────────────┘
                  ↓
┌─────────────────────────────────────────┐
│      lws_media (Media Layer)            │
│  ┌────────┐  ┌────────┐  ┌────────┐    │
│  │  File  │  │ Memory │  │ Device │    │
│  │  I/O   │  │ Buffer │  │Mic/Spk │    │
│  └────────┘  └────────┘  └────────┘    │
└─────────────────────────────────────────┘
```

## 核心设计原则

### 0. 传输层抽象（关键创新✨）

**设计动机**: 嵌入式系统可能使用多种通信方式（TCP/UDP、MQTT、串口等），而不仅限于标准socket。

**实现方式**: 通过虚函数表模式实现传输层抽象，上层代码（lws_client）完全不依赖具体传输实现。

```c
// 传输层抽象接口
typedef struct {
    int (*connect)(lws_transport_t* transport);
    int (*send)(lws_transport_t* transport, const void* data, int len);
    int (*poll)(lws_transport_t* transport, int timeout_ms);
    // ... 其他操作
} lws_transport_ops_t;

// 使用时自动多态
lws_transport_send(transport, data, len);
// → 自动调用 transport->ops->send()
```

**已实现的传输**:
- ✅ **lws_transport_tcp.c**: TCP/UDP socket（标准SIP）
- ✅ **lws_transport_mqtt.c**: MQTT pub/sub（IoT场景）
- 🔄 **lws_transport_serial.c**: 串口通信（工业场景）- 可扩展
- 🔄 **lws_transport_custom.c**: 自定义协议 - 可扩展

**切换传输层示例**:
```c
// 标准TCP/UDP
transport = lws_transport_tcp_create(&config, &handler);

// 或者使用MQTT（只需改一行）
transport = lws_transport_mqtt_create(&config, &handler);

// 上层代码完全不变！
lws_transport_connect(transport);
lws_transport_send(transport, data, len);
```

详见: [TRANSPORT_DESIGN.md](TRANSPORT_DESIGN.md)

### 1. Handler模式（参考libsip）

所有回调通过结构体一次性注册，而不是分散在多个参数中：

```c
// ✅ 好的设计 (v2.0)
struct lws_client_handler_t {
    void (*on_reg_state)(void* param, lws_reg_state_t state, int code);
    void (*on_call_state)(void* param, const char* peer, lws_call_state_t state);
    void (*on_incoming_call)(void* param, const char* from, const char* to, const char* sdp, int sdp_len);
    void (*on_error)(void* param, int errcode, const char* description);
    void* param;
};

lws_client_t* lws_client_create(const lws_config_t* config,
                                const lws_client_handler_t* handler);
```

### 2. Payload封装（参考librtp）

自动处理RTP分包/组包，用户只需要处理完整帧：

```c
// 发送端：输入完整帧 → 自动分成多个RTP包
lws_payload_encoder_t* encoder = lws_payload_encoder_create(
    96, "H264", ssrc, seq, rtp_packet_cb, param);
lws_payload_encode(encoder, frame_data, frame_size, timestamp);

// 接收端：输入RTP包 → 自动组包输出完整帧
lws_payload_decoder_t* decoder = lws_payload_decoder_create(
    96, "H264", frame_cb, param);
lws_payload_decode(decoder, rtp_packet, packet_size);
```

### 3. 清晰的职责分离

| 层次 | 职责 | 不负责 |
|-----|------|--------|
| **lws_client** | SIP信令、会话管理 | RTP传输、媒体编解码 |
| **lws_session** | RTP会话、SDP协商 | SIP信令、媒体I/O |
| **lws_media** | 媒体源/目标 | RTP封装、SIP信令 |

### 4. 使用OSAL接口

所有平台相关操作通过osal抽象：

```c
#include "../../osal/include/lws_mem.h"      // 内存管理
#include "../../osal/include/lws_log.h"      // 日志系统
#include "../../osal/include/lws_mutex.h"    // 互斥锁
#include "../../osal/include/lws_thread.h"   // 线程
```

## 目录结构

```
v2.0/
├── include/                      # 公共头文件 (9个)
│   ├── lws_client.h             # SIP客户端核心接口
│   ├── lws_uac.h                # User Agent Client
│   ├── lws_uas.h                # User Agent Server
│   ├── lws_session.h            # RTP会话管理
│   ├── lws_payload.h            # RTP payload封装
│   ├── lws_media.h              # 媒体源/目标
│   ├── lws_transport.h          # ✨ 传输层抽象接口
│   ├── lws_types.h              # 类型定义
│   └── lws_error.h              # 错误码
│
├── src/                         # 实现文件 (9个)
│   ├── lws_client.c             # SIP客户端实现
│   ├── lws_uac.c                # UAC实现
│   ├── lws_uas.c                # UAS实现
│   ├── lws_session.c            # RTP会话实现
│   ├── lws_payload.c            # Payload封装实现
│   ├── lws_media.c              # 媒体I/O实现
│   ├── lws_transport_tcp.c      # ✨ TCP/UDP传输实现
│   ├── lws_transport_mqtt.c     # ✨ MQTT传输实现（示例）
│   └── lws_error.c              # 错误码映射
│
├── example.c                    # 使用示例
├── README.md                    # 本文档
└── TRANSPORT_DESIGN.md          # ✨ 传输层设计文档
```

## 使用示例

### 1. 创建SIP客户端并注册

```c
#include "include/lws_client.h"

// 配置
lws_config_t config = {
    .server_host = "192.168.1.100",
    .server_port = 5060,
    .username = "1002",
    .password = "1234",
    .enable_audio = 1,
    .audio_codec = LWS_AUDIO_CODEC_PCMU,
};

// 回调
lws_client_handler_t handler = {
    .on_reg_state = on_reg_state,
    .on_call_state = on_call_state,
    .on_incoming_call = on_incoming_call,
    .on_error = on_error,
};

// 创建并启动
lws_client_t* client = lws_client_create(&config, &handler);
lws_client_start(client);

// 注册
lws_uac_register(client);
```

### 2. 发起呼叫（简化API）

```c
// 一行代码发起呼叫（自动创建RTP会话）
lws_session_t* session = lws_call(client, "sip:1001@192.168.1.100");

// 通话中...

// 挂断
lws_hangup(client, session);
```

### 3. 接收呼叫

```c
// 在on_incoming_call回调中
static void on_incoming_call(void* param,
    const char* from, const char* to,
    const char* sdp, int sdp_len)
{
    printf("Incoming call from: %s\n", from);

    // 应答呼叫
    lws_session_t* session = lws_answer(g_client, from);

    // 或者拒绝
    // lws_uas_reject(g_client, from, 486);  // Busy Here
}
```

### 4. 高级使用：手动创建RTP会话

```c
// 创建媒体源（从文件读取）
lws_media_config_t media_config = {
    .type = LWS_MEDIA_TYPE_FILE,
    .file_path = "audio.wav",
    .loop = 1,
    .audio_codec = LWS_AUDIO_CODEC_PCMU,
    .sample_rate = 8000,
    .channels = 1,
};
lws_media_t* media = lws_media_create(&media_config);

// 创建RTP会话
lws_session_handler_t session_handler = {
    .on_media_ready = on_media_ready,
    .on_audio_frame = on_audio_frame,
    .param = NULL,
};
lws_session_t* session = lws_session_create(&config, &session_handler);

// 绑定媒体源
lws_session_set_media_source(session, media);

// 生成SDP offer
char sdp[4096];
int sdp_len = lws_session_generate_sdp_offer(session, "192.168.1.2", sdp, sizeof(sdp));

// 发送INVITE
lws_uac_invite(client, peer_uri, session);

// 启动会话
lws_session_start(session);
```

### 5. 事件循环

```c
// 主循环
while (running) {
    // 处理SIP和RTP事件（100ms超时）
    int ret = lws_client_loop(client, 100);
    if (ret < 0) {
        fprintf(stderr, "Error: 0x%08x\n", ret);
        break;
    }

    // 处理其他任务...
}
```

## API对比：v1.0 vs v2.0

| 功能 | v1.0 | v2.0 | 改进 |
|-----|------|------|------|
| **传输层** | 硬编码socket | `lws_transport_*` 抽象 | ✨ 支持多种传输 |
| **回调注册** | 分散在多个参数 | Handler结构体 | 更清晰 |
| **发起呼叫** | 多步操作 | `lws_call()` | 一行代码 |
| **RTP封装** | 手动处理 | `lws_payload_*` | 自动分包/组包 |
| **错误处理** | printf | 错误码+回调 | 统一管理 |
| **媒体抽象** | 文件耦合 | `lws_media_*` | 支持多种源 |
| **线程安全** | 无 | mutex保护 | 线程安全 |

## 传输层使用场景

| 场景 | 传输选择 | 说明 |
|-----|---------|------|
| **标准SIP** | `lws_transport_tcp` | 标准UDP/TCP socket |
| **IoT设备在NAT后** | `lws_transport_mqtt` | 通过MQTT broker转发 |
| **工业现场无网络** | `lws_transport_serial` | RS232/RS485串口 |
| **自定义加密** | 自己实现 | 按需扩展 |

## 与libsip/librtp的关系

v2.0是对media-server库的高层封装：

```
v2.0 API               media-server库
─────────              ──────────────
lws_client      →      libsip (sip-uac.h, sip-uas.h)
lws_session     →      librtp (rtp.h)
lws_payload     →      librtp (rtp-payload.h)
lws_media       →      (用户实现)
```

## 下一步工作

v2.0当前是**框架代码**，需要补充实现：

1. **lws_client.c**:
   - 集成libsip的`sip_agent_create()`
   - 实现socket管理和事件循环
   - 实现`lws_client_loop()`

2. **lws_session.c**:
   - 集成librtp的`rtp_create()`
   - 实现RTP/RTCP socket收发
   - 完善SDP生成/解析（使用librtsp的sdp.h）

3. **lws_uac.c / lws_uas.c**:
   - 实现`sip_uac_register()`、`sip_uac_invite()`等
   - 处理SIP响应回调

4. **lws_media.c**:
   - 支持WAV文件读写
   - 支持ALSA/CoreAudio设备（可选）

5. **测试**:
   - 与Asterisk/FreeSWITCH互通测试
   - 压力测试

## 编译

```bash
# 当前v2.0需要集成到项目构建系统
# TODO: 创建CMakeLists.txt for v2.0
```

## 许可证

与lwsip项目相同

---

**设计参考**:
- libsip: SIP协议栈设计、Handler模式
- librtp: RTP payload封装、自动分包组包
- librtsp: SDP解析、RTSP客户端设计

**设计目标**:
- ✅ 简单易用：一行代码发起呼叫
- ✅ 清晰架构：SIP/RTP/Media三层分离
- ✅ 跨平台：使用OSAL抽象
- ✅ 可扩展：支持多种媒体源和编解码器
