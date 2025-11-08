# lwsip 媒体传输层设计

## 1. 设计目标

使用 libice 和 librtp 为 lwsip 构建一个高效、灵活的媒体传输层，满足以下要求：

- **NAT 穿透**：利用 libice 实现 ICE/STUN/TURN 完整的 NAT 穿透能力
- **可靠传输**：通过 librtp 提供标准的 RTP/RTCP 媒体流传输
- **SIP 集成**：与现有 SIP 信令层无缝协作，处理 SDP 协商
- **简单易用**：为应用层提供清晰、简洁的 API
- **资源高效**：适合嵌入式系统，内存和 CPU 占用小
- **可扩展性**：支持音频、视频，未来可扩展到屏幕共享等

## 2. 架构层次

```
┌─────────────────────────────────────────────────────┐
│          Application Layer (用户代码)                │
│   lws_call(), lws_answer(), lws_hangup()            │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│         SIP Signaling Layer (lws_agent)             │
│   REGISTER, INVITE, ACK, BYE, CANCEL                │
│   SDP Offer/Answer                                  │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│      Media Session Layer (NEW: lws_media_session)   │  ← 本设计重点
│   - ICE 协商和连接建立                               │
│   - RTP/RTCP 会话管理                               │
│   - 媒体流收发                                       │
│   - 统计和监控                                       │
└─────────────────────────────────────────────────────┘
          ↓                              ↓
┌──────────────────────┐      ┌──────────────────────┐
│   libice (NAT穿透)    │      │   librtp (媒体流)     │
│  - ICE agent         │      │  - RTP session       │
│  - STUN/TURN         │      │  - RTCP              │
│  - Candidate gather  │      │  - 编解码适配         │
└──────────────────────┘      └──────────────────────┘
          ↓                              ↓
┌─────────────────────────────────────────────────────┐
│            Network Layer (UDP/TCP)                  │
└─────────────────────────────────────────────────────┘
```

## 3. 核心设计原则

### 3.1 职责分离

- **lws_agent**: 只负责 SIP 信令，不直接处理媒体
- **lws_media_session**: 媒体会话管理，封装 ICE + RTP
- **libice**: 专注 NAT 穿透和连接建立
- **librtp**: 专注 RTP/RTCP 协议处理

### 3.2 生命周期管理

```
SIP INVITE 发送
    ↓
创建 lws_media_session
    ↓
ICE gathering (收集 candidates)
    ↓
生成 SDP offer (包含 ICE candidates)
    ↓
SIP 交换 SDP
    ↓
ICE connectivity checks
    ↓
ICE connected
    ↓
启动 RTP session
    ↓
媒体流收发
    ↓
SIP BYE
    ↓
停止 RTP session
    ↓
销毁 lws_media_session
```

### 3.3 事件驱动

- 使用回调通知应用层状态变化
- ICE 状态：gathering, checking, connected, failed
- RTP 事件：媒体帧到达、RTCP 报告、错误
- 统一的 poll 机制，方便集成到主事件循环

### 3.4 传输抽象

- libice 本身已经抽象了传输层（通过 handler 的 send 函数）
- 我们不需要重复抽象，直接利用 libice 的传输抽象能力
- RTP 数据通过 ICE 已建立的连接发送

## 4. 接口设计

### 4.1 数据结构

```c
/**
 * 媒体会话句柄（不透明）
 */
typedef struct lws_media_session lws_media_session_t;

/**
 * ICE 配置
 */
typedef struct {
    // STUN 服务器
    const char* stun_server;   // "stun.l.google.com:19302"
    uint16_t stun_port;

    // TURN 服务器（可选）
    const char* turn_server;   // "turn.example.com:3478"
    uint16_t turn_port;
    const char* turn_username;
    const char* turn_password;

    // ICE 角色
    int controlling;  // 1=controlling, 0=controlled

    // 候选地址过滤
    int enable_host_candidates;     // 本地地址候选
    int enable_srflx_candidates;    // STUN 反射候选
    int enable_relay_candidates;    // TURN 中继候选
} lws_ice_config_t;

/**
 * 编解码配置
 */
typedef struct {
    // 音频
    int audio_payload_type;    // RTP payload type, 例如 0=PCMU, 8=PCMA
    int audio_sample_rate;     // 采样率，8000/16000/48000
    int audio_channels;        // 声道数，1=单声道，2=立体声

    // 视频（可选）
    int video_payload_type;    // 例如 96=H.264
    int video_width;
    int video_height;
    int video_fps;
} lws_codec_config_t;

/**
 * 媒体会话配置
 */
typedef struct {
    lws_ice_config_t ice;
    lws_codec_config_t codec;

    // 本地绑定
    const char* local_ip;      // 本地 IP，NULL=自动检测
    uint16_t local_rtp_port;   // 本地 RTP 端口，0=自动分配

    // 用户数据
    void* userdata;
} lws_media_session_config_t;

/**
 * ICE 状态
 */
typedef enum {
    LWS_ICE_STATE_IDLE = 0,
    LWS_ICE_STATE_GATHERING,   // 正在收集 candidates
    LWS_ICE_STATE_CHECKING,    // 正在连接性检查
    LWS_ICE_STATE_CONNECTED,   // 已连接
    LWS_ICE_STATE_FAILED       // 失败
} lws_ice_state_t;

/**
 * 媒体统计
 */
typedef struct {
    // RTP 统计
    uint64_t tx_packets;       // 发送包数
    uint64_t tx_bytes;         // 发送字节数
    uint64_t rx_packets;       // 接收包数
    uint64_t rx_bytes;         // 接收字节数
    uint32_t rx_lost_packets;  // 丢包数

    // ICE 统计
    int ice_selected_pair_type;  // 选中的候选对类型
    char ice_local_addr[64];     // 本地地址
    char ice_remote_addr[64];    // 远端地址

    // RTCP 统计
    uint32_t rtcp_rtt_ms;        // 往返延迟（毫秒）
    uint8_t rtcp_fraction_lost;  // 丢包率（0-255）
} lws_media_stats_t;
```

### 4.2 事件回调

```c
/**
 * 媒体会话事件回调
 */
typedef struct {
    /**
     * ICE 状态变化
     * @param session 会话句柄
     * @param state 新状态
     * @param userdata 用户数据
     */
    void (*on_ice_state)(lws_media_session_t* session,
                         lws_ice_state_t state,
                         void* userdata);

    /**
     * ICE candidates 收集完成
     * 应用层应该通过 SIP 发送 SDP
     * @param session 会话句柄
     * @param sdp 本地 SDP（包含 ICE candidates）
     * @param sdp_len SDP 长度
     * @param userdata 用户数据
     */
    void (*on_ice_gathered)(lws_media_session_t* session,
                            const char* sdp,
                            int sdp_len,
                            void* userdata);

    /**
     * 音频帧到达
     * @param session 会话句柄
     * @param data 音频数据
     * @param bytes 数据长度
     * @param timestamp RTP 时间戳
     * @param userdata 用户数据
     */
    void (*on_audio_frame)(lws_media_session_t* session,
                           const void* data,
                           int bytes,
                           uint32_t timestamp,
                           void* userdata);

    /**
     * 视频帧到达
     * @param session 会话句柄
     * @param data 视频数据
     * @param bytes 数据长度
     * @param timestamp RTP 时间戳
     * @param userdata 用户数据
     */
    void (*on_video_frame)(lws_media_session_t* session,
                           const void* data,
                           int bytes,
                           uint32_t timestamp,
                           void* userdata);

    /**
     * RTCP 报告到达
     * @param session 会话句柄
     * @param stats 统计信息
     * @param userdata 用户数据
     */
    void (*on_rtcp_report)(lws_media_session_t* session,
                           const lws_media_stats_t* stats,
                           void* userdata);

    /**
     * 错误发生
     * @param session 会话句柄
     * @param errcode 错误码
     * @param description 错误描述
     * @param userdata 用户数据
     */
    void (*on_error)(lws_media_session_t* session,
                     int errcode,
                     const char* description,
                     void* userdata);

    void* userdata;
} lws_media_session_handler_t;
```

### 4.3 核心 API

```c
/**
 * @brief 创建媒体会话
 * @param config 配置
 * @param handler 事件回调
 * @return 会话句柄，失败返回 NULL
 */
lws_media_session_t* lws_media_session_create(
    const lws_media_session_config_t* config,
    const lws_media_session_handler_t* handler);

/**
 * @brief 销毁媒体会话
 * @param session 会话句柄
 */
void lws_media_session_destroy(lws_media_session_t* session);

/**
 * @brief 启动 ICE gathering（作为 Offerer）
 * 调用后会开始收集 ICE candidates
 * 完成后触发 on_ice_gathered 回调
 * @param session 会话句柄
 * @return 0 成功，<0 失败
 */
int lws_media_session_start_ice_gathering(lws_media_session_t* session);

/**
 * @brief 处理远端 SDP（作为 Answerer）
 * 解析远端 SDP，提取 ICE candidates 和编解码信息
 * 然后生成本地 SDP answer
 * @param session 会话句柄
 * @param remote_sdp 远端 SDP
 * @param remote_len SDP 长度
 * @param local_sdp 输出本地 SDP [out]
 * @param local_len 本地 SDP 缓冲区大小
 * @return 实际 SDP 长度，<0 表示失败
 */
int lws_media_session_process_remote_sdp(
    lws_media_session_t* session,
    const char* remote_sdp,
    int remote_len,
    char* local_sdp,
    int local_len);

/**
 * @brief 处理远端 SDP answer（Offerer 收到 answer 后调用）
 * @param session 会话句柄
 * @param remote_sdp 远端 SDP answer
 * @param remote_len SDP 长度
 * @return 0 成功，<0 失败
 */
int lws_media_session_set_remote_sdp(
    lws_media_session_t* session,
    const char* remote_sdp,
    int remote_len);

/**
 * @brief 启动 ICE connectivity checks
 * 在交换完 SDP 后调用，开始连接性检查
 * @param session 会话句柄
 * @return 0 成功，<0 失败
 */
int lws_media_session_start_ice_checks(lws_media_session_t* session);

/**
 * @brief 发送音频数据
 * @param session 会话句柄
 * @param data 音频数据
 * @param bytes 数据长度
 * @param timestamp RTP 时间戳（可选，0=自动生成）
 * @return 发送字节数，<0 失败
 */
int lws_media_session_send_audio(
    lws_media_session_t* session,
    const void* data,
    int bytes,
    uint32_t timestamp);

/**
 * @brief 发送视频数据
 * @param session 会话句柄
 * @param data 视频数据
 * @param bytes 数据长度
 * @param timestamp RTP 时间戳（可选，0=自动生成）
 * @return 发送字节数，<0 失败
 */
int lws_media_session_send_video(
    lws_media_session_t* session,
    const void* data,
    int bytes,
    uint32_t timestamp);

/**
 * @brief 事件轮询
 * 处理 ICE 和 RTP 事件，调用相应回调
 * @param session 会话句柄
 * @param timeout_ms 超时（毫秒），0=非阻塞，-1=无限等待
 * @return >0 处理的事件数，0=超时，<0=错误
 */
int lws_media_session_poll(lws_media_session_t* session, int timeout_ms);

/**
 * @brief 获取统计信息
 * @param session 会话句柄
 * @param stats 统计信息 [out]
 * @return 0 成功，<0 失败
 */
int lws_media_session_get_stats(
    lws_media_session_t* session,
    lws_media_stats_t* stats);

/**
 * @brief 获取 ICE 状态
 * @param session 会话句柄
 * @return ICE 状态
 */
lws_ice_state_t lws_media_session_get_ice_state(lws_media_session_t* session);
```

## 5. 关键设计要点

### 5.1 SDP 处理

**问题**：如何在 SDP 中嵌入 ICE candidates？

**解决方案**：
- libice 收集到的 candidates 需要转换为 SDP 的 `a=candidate:` 行
- SDP 需要包含 `a=ice-ufrag` 和 `a=ice-pwd` 属性
- 我们的 `lws_media_session` 内部处理这些转换

示例 SDP：
```
v=0
o=- 123456 789012 IN IP4 192.168.1.100
s=lwsip
c=IN IP4 192.168.1.100
t=0 0
a=ice-ufrag:F7gI
a=ice-pwd:x9cml/YzichV2+XlhiMu8g
a=ice-options:trickle
m=audio 54400 RTP/AVP 0
a=rtpmap:0 PCMU/8000
a=candidate:1 1 UDP 2130706431 192.168.1.100 54400 typ host
a=candidate:2 1 UDP 1694498815 203.0.113.5 54400 typ srflx raddr 192.168.1.100 rport 54400
a=candidate:3 1 UDP 16777215 198.51.100.10 49352 typ relay raddr 203.0.113.5 rport 54400
```

### 5.2 ICE 与 RTP 的协作

**关键点**：
1. ICE 建立底层连接通道
2. RTP 复用 ICE 的通道发送媒体数据

**实现方式**：
```c
// 内部实现示例
struct lws_media_session {
    ice_agent_t* ice_agent;        // libice agent
    rtp_session_t* rtp_session;    // librtp session

    // ICE 的 send handler
    static int ice_send_handler(void* param, int protocol,
                                const struct sockaddr* local,
                                const struct sockaddr* remote,
                                const void* data, int bytes) {
        // 直接发送到网络
        return sendto(sockfd, data, bytes, 0, remote, ...);
    }

    // ICE 的 ondata handler
    static void ice_ondata_handler(void* param, uint8_t stream,
                                   uint16_t component,
                                   const void* data, int bytes) {
        lws_media_session_t* session = param;

        // 检查是否是 RTP/RTCP 数据
        if (is_rtp_packet(data, bytes)) {
            // 交给 librtp 处理
            rtp_session_input(session->rtp_session, data, bytes);
        } else if (is_rtcp_packet(data, bytes)) {
            rtcp_input(session->rtp_session, data, bytes);
        }
    }
};

// RTP 发送通过 ICE
int lws_media_session_send_audio(lws_media_session_t* session,
                                  const void* data, int bytes,
                                  uint32_t timestamp) {
    // 1. librtp 打包 RTP
    uint8_t rtp_packet[2048];
    int rtp_len = rtp_pack(session->rtp_session, data, bytes,
                           timestamp, rtp_packet, sizeof(rtp_packet));

    // 2. 通过 ICE 发送
    return ice_agent_send(session->ice_agent, 0, 1, rtp_packet, rtp_len);
}
```

### 5.3 多路复用（Multiplexing）

**问题**：如何在同一个 ICE 连接上传输 RTP 和 RTCP？

**解决方案**：使用 RTP/RTCP 多路复用
- RTP 和 RTCP 共用一个端口
- 通过数据包的第二个字节区分：
  - RTP: 第二字节的 payload type < 64 或 > 95
  - RTCP: 第二字节的 packet type 在 200-204 范围内

```c
static int is_rtp_packet(const void* data, int bytes) {
    if (bytes < 2) return 0;
    const uint8_t* buf = data;
    uint8_t pt = buf[1] & 0x7F;
    return pt < 64 || pt > 95;  // RTP payload type
}

static int is_rtcp_packet(const void* data, int bytes) {
    if (bytes < 2) return 0;
    const uint8_t* buf = data;
    uint8_t pt = buf[1];
    return pt >= 200 && pt <= 204;  // RTCP packet type
}
```

### 5.4 线程安全

**设计原则**：
- 所有 API 调用必须在同一线程
- 回调在调用线程上下文中执行
- 不提供内部锁（由应用层控制线程同步，减少开销）

如果应用需要多线程访问：
```c
// 应用层负责加锁
lws_mutex_lock(my_mutex);
lws_media_session_send_audio(session, data, len, ts);
lws_mutex_unlock(my_mutex);
```

### 5.5 错误处理

**错误码定义**：
```c
#define LWS_MEDIA_ERR_NONE           0
#define LWS_MEDIA_ERR_INVALID_PARAM  -1
#define LWS_MEDIA_ERR_NO_MEMORY      -2
#define LWS_MEDIA_ERR_ICE_FAILED     -3
#define LWS_MEDIA_ERR_RTP_FAILED     -4
#define LWS_MEDIA_ERR_TIMEOUT        -5
#define LWS_MEDIA_ERR_NOT_CONNECTED  -6
```

**错误传播**：
- 同步错误：通过返回值
- 异步错误：通过 `on_error` 回调

## 6. 使用示例

### 6.1 主叫方（Caller/Offerer）

```c
#include "lws_agent.h"
#include "lws_media_session.h"

// 1. 配置
lws_media_session_config_t config = {
    .ice = {
        .stun_server = "stun.l.google.com",
        .stun_port = 19302,
        .controlling = 1,  // 主叫方是 controlling
        .enable_host_candidates = 1,
        .enable_srflx_candidates = 1,
        .enable_relay_candidates = 0,
    },
    .codec = {
        .audio_payload_type = 0,    // PCMU
        .audio_sample_rate = 8000,
        .audio_channels = 1,
    },
    .local_ip = NULL,  // 自动检测
    .local_rtp_port = 0,  // 自动分配
    .userdata = my_app_data,
};

// 2. 事件回调
lws_media_session_handler_t handler = {
    .on_ice_state = my_ice_state_cb,
    .on_ice_gathered = my_ice_gathered_cb,
    .on_audio_frame = my_audio_frame_cb,
    .on_error = my_error_cb,
    .userdata = my_app_data,
};

// 3. 创建会话
lws_media_session_t* media_session =
    lws_media_session_create(&config, &handler);

// 4. 开始 ICE gathering
lws_media_session_start_ice_gathering(media_session);

// 5. ICE gathered 回调中发送 INVITE
void my_ice_gathered_cb(lws_media_session_t* session,
                        const char* sdp, int sdp_len,
                        void* userdata) {
    // 通过 SIP 发送 INVITE，包含 SDP
    lws_call(sip_agent, "sip:callee@example.com", sdp, sdp_len);
}

// 6. 收到 SIP 200 OK (answer) 后
void on_sip_call_answered(const char* remote_sdp, int sdp_len) {
    // 设置远端 SDP
    lws_media_session_set_remote_sdp(media_session, remote_sdp, sdp_len);

    // 开始 ICE 连接性检查
    lws_media_session_start_ice_checks(media_session);
}

// 7. ICE 连接成功后发送媒体
void my_ice_state_cb(lws_media_session_t* session,
                     lws_ice_state_t state,
                     void* userdata) {
    if (state == LWS_ICE_STATE_CONNECTED) {
        printf("ICE connected! Start sending media...\n");
        // 可以开始发送音频了
    }
}

// 8. 主循环
while (running) {
    // 处理 SIP 事件
    lws_agent_loop(sip_agent, 0);

    // 处理媒体事件
    lws_media_session_poll(media_session, 0);

    // 读取麦克风，发送音频
    uint8_t audio_data[160];
    int len = read_microphone(audio_data, sizeof(audio_data));
    if (len > 0) {
        lws_media_session_send_audio(media_session, audio_data, len, 0);
    }
}

// 9. 挂断
lws_hangup(sip_agent, session);
lws_media_session_destroy(media_session);
```

### 6.2 被叫方（Callee/Answerer）

```c
// 1. 收到 INVITE
void on_incoming_call(const char* from, const char* to,
                      const char* remote_sdp, int sdp_len) {
    // 创建媒体会话
    lws_media_session_config_t config = {
        .ice.controlling = 0,  // 被叫方是 controlled
        // ... 其他配置
    };

    media_session = lws_media_session_create(&config, &handler);

    // 处理远端 SDP，生成 answer
    char local_sdp[2048];
    int local_len = lws_media_session_process_remote_sdp(
        media_session, remote_sdp, sdp_len,
        local_sdp, sizeof(local_sdp));

    if (local_len > 0) {
        // 开始 ICE 检查
        lws_media_session_start_ice_checks(media_session);

        // 发送 SIP 200 OK
        lws_answer(sip_agent, from, local_sdp, local_len);
    }
}

// 2. 主循环同 caller
```

## 7. 内部实现要点

### 7.1 SDP 解析与生成

需要实现：
```c
// 内部函数
static int sdp_parse(const char* sdp, int len, sdp_info_t* info);
static int sdp_generate(const sdp_info_t* info, char* buf, int buflen);
static int sdp_add_ice_candidates(char* sdp, int len,
                                   const ice_candidate_t* candidates,
                                   int candidate_count);
```

### 7.2 ICE Candidates 管理

```c
struct lws_media_session {
    // ICE
    ice_agent_t* ice;
    ice_candidate_t local_candidates[32];
    int local_candidate_count;
    ice_candidate_t remote_candidates[32];
    int remote_candidate_count;

    // RTP
    rtp_session_t* rtp;

    // 状态
    lws_ice_state_t ice_state;
    int ice_connected;

    // 配置和回调
    lws_media_session_config_t config;
    lws_media_session_handler_t handler;
};
```

### 7.3 定时器管理

ICE 和 RTP 都需要定时器：
- ICE: connectivity check 定时器
- RTCP: 定期发送 SR/RR 报告

可以集成到 `lws_media_session_poll()` 中：
```c
int lws_media_session_poll(lws_media_session_t* session, int timeout_ms) {
    struct pollfd fds[2];
    int nfds = 0;

    // ICE socket
    fds[nfds++] = (struct pollfd){
        .fd = session->ice_sockfd,
        .events = POLLIN,
    };

    // 计算下次定时器触发时间
    int next_timer_ms = MIN(ice_next_timer(session->ice),
                            rtcp_next_timer(session->rtp));

    int actual_timeout = (timeout_ms < 0) ? next_timer_ms :
                         MIN(timeout_ms, next_timer_ms);

    int ret = poll(fds, nfds, actual_timeout);

    if (ret > 0) {
        // 处理 ICE 数据
        if (fds[0].revents & POLLIN) {
            uint8_t buf[2048];
            int len = recv(session->ice_sockfd, buf, sizeof(buf), 0);
            ice_agent_input(session->ice, buf, len);
        }
    }

    // 处理定时器
    ice_agent_tick(session->ice);
    rtcp_tick(session->rtp);

    return ret;
}
```

## 8. 优化建议

### 8.1 内存优化

- 使用内存池管理 candidates 和 RTP packets
- 避免频繁分配/释放
- 预分配固定大小的缓冲区

### 8.2 性能优化

- 零拷贝：直接在 RTP buffer 上操作
- 批处理：一次 poll 处理多个 RTP 包
- SIMD：音频处理使用 SIMD 指令（如果适用）

### 8.3 功耗优化（嵌入式）

- 自适应轮询间隔：无数据时降低轮询频率
- 睡眠模式：长时间静音时进入低功耗模式
- DTIM 对齐：Wi-Fi 环境下与 DTIM 周期对齐

## 9. 测试建议

### 9.1 单元测试

- SDP 解析和生成
- ICE candidates 转换
- RTP 打包/解包

### 9.2 集成测试

- 与标准 SIP/RTP 客户端互通（如 pjsua, linphone）
- NAT 穿透场景测试
- 丢包和抖动模拟

### 9.3 压力测试

- 长时间运行稳定性
- 内存泄漏检测
- 多会话并发

## 10. 总结

这个设计的核心优势：

1. **清晰的分层**：SIP、Media Session、ICE/RTP 各司其职
2. **最小暴露**：应用层只需关心媒体会话，ICE 细节被封装
3. **事件驱动**：回调机制，易于集成到各种事件循环
4. **灵活配置**：支持各种 ICE 和编解码配置
5. **可扩展性**：未来可添加视频、DTLS-SRTP 等特性

**下一步**：
- 实现 `lws_media_session.c`
- 实现 SDP 工具函数
- 编写示例程序
- 集成测试
