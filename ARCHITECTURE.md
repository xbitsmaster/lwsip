# lwsip 架构简介

## 整体架构

lwsip 采用分层架构设计，各层职责清晰，从上到下分为：SIP层、媒体层、传输层。

```
┌─────────────────────────────────────────┐
│          Application Layer              │  用户应用
│      (lws_client.h - 对外API)           │
└──────────────┬──────────────────────────┘
               │
┌──────────────┴──────────────────────────┐
│           SIP Signaling Layer           │  SIP信令层
│  ┌──────────┐  ┌──────────┐             │
│  │   UAC    │  │   UAS    │             │  呼叫控制
│  │  发起呼叫│  │  接收呼叫│             │  (INVITE/BYE/ACK等)
│  └──────────┘  └──────────┘             │
│         sip_transport.h                 │  SIP消息传输(UDP/TCP)
└──────────────┬──────────────────────────┘
               │ SDP协商
┌──────────────┴──────────────────────────┐
│          Media Session Layer            │  媒体会话层
│      (media_session.h)                  │
│  ┌──────────────┐  ┌──────────────┐    │
│  │ Audio Stream │  │ Video Stream │    │  SDP处理、编解码参数
│  └──────────────┘  └──────────────┘    │
│       rtp_handler.h                     │  RTP流管理
└──────────────┬──────────────────────────┘
               │
┌──────────────┴──────────────────────────┐
│         RTP/RTCP Transport Layer        │  媒体传输层
│      (rtp_transport.h)                  │
│  ┌──────────┐  ┌──────────┐             │
│  │   RTP    │  │  RTCP    │             │  音视频数据传输
│  │  媒体数据│  │  控制信息│             │  UDP socket
│  └──────────┘  └──────────┘             │
└─────────────────────────────────────────┘
```

## 各层说明

### 1. SIP 信令层
**职责**: 负责呼叫建立、维持、终止等信令控制

- **sip_transport**: SIP消息传输，支持UDP/TCP协议
- **UAC/UAS**: 用户代理客户端/服务器，处理呼叫发起和接收
- **核心功能**: REGISTER注册、INVITE呼叫建立、BYE挂断、ACK确认等

### 2. 媒体会话层
**职责**: 管理音视频会话，协商媒体参数

- **media_session**: 音视频会话管理，维护音频/视频流信息
- **SDP协商**: 处理媒体格式、端口、编解码器等参数协商
- **rtp_handler**: RTP流处理，封装/解封装RTP包
- **编解码支持**: PCMU/PCMA(音频)、H.264/H.265(视频)

### 3. RTP/RTCP 传输层
**职责**: 实时传输音视频数据流

- **rtp_transport**: RTP数据包的发送和接收
- **RTP**: 传输实际的音视频媒体数据
- **RTCP**: 传输质量反馈和控制信息(SR/RR/SDES等)
- **端口分配**: RTP使用偶数端口，RTCP使用RTP端口+1

## 调用流程示例

### 呼出流程
```
1. lws_call()
   → SIP INVITE (via sip_transport)
   → SDP offer (via media_session)

2. 收到 200 OK + SDP answer
   → media_session_process_sdp()
   → rtp_transport_set_remote()

3. media_session_start()
   → rtp_transport_start()
   → 开始音视频传输
```

### 呼入流程
```
1. 收到 INVITE + SDP offer
   → UAS 处理
   → media_session_process_sdp()

2. lws_answer()
   → SIP 200 OK (via sip_transport)
   → SDP answer (via media_session)

3. media_session_start()
   → rtp_transport_start()
   → 开始音视频传输
```

## 关键特性

- **分层设计**: 各层职责明确，便于移植和测试
- **传输独立**: SIP和RTP使用独立的transport层，互不干扰
- **协议支持**: SIP信令(RFC3261)、RTP/RTCP媒体传输(RFC3550)
- **编解码**: 支持多种音视频编解码格式
- **嵌入式优化**: 轻量级设计，适合RTOS环境运行
