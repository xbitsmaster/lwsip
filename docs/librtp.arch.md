# librtp 架构与使用指南

## 项目概述

**librtp** 是 media-server 项目中的 RTP/RTCP 协议栈实现库。

- **项目地址**: https://github.com/ireader/media-server
- **协议标准**: RFC 3550 (RTP/RTCP)
- **功能**: 提供 RTP/RTCP 包的发送、接收、序列化、反序列化以及 RTCP 报告生成等功能

## 核心概念

### 1. RTP (Real-time Transport Protocol)

RTP 是用于在 IP 网络上传输音频和视频的协议(RFC 3550)。

**主要特性**:
- 序列号(Sequence Number): 用于检测丢包和重排序
- 时间戳(Timestamp): 用于媒体同步
- SSRC(Synchronization Source): 标识RTP流的源
- Payload Type: 标识媒体编码格式

### 2. RTCP (RTP Control Protocol)

RTCP 提供 RTP 会话的控制和统计信息。

**RTCP 包类型**:
- **SR (Sender Report)**: 发送方报告，包含发送统计
- **RR (Receiver Report)**: 接收方报告，包含接收质量统计
- **SDES (Source Description)**: 源描述信息(CNAME, NAME等)
- **BYE**: 通知会话结束
- **APP**: 应用特定消息
- **RTPFB**: RTP反馈消息(NACK等)
- **PSFB**: Payload特定反馈(PLI, FIR等)
- **XR**: 扩展报告

### 3. RTP Member

RTP 成员代表会话中的一个参与者(发送方或接收方)。

**成员信息**:
- SSRC: 同步源标识符
- 序列号状态: 用于检测丢包和乱序
- 时间戳信息: 用于计算抖动
- RTCP报告: SR/RR统计信息
- SDES信息: CNAME, NAME等描述信息

### 4. RTP Payload

Payload 封装了媒体编解码的逻辑。

**支持的编解码器**:
- 音频: PCMU, PCMA, G.722, G.729, Opus, MP3
- 视频: H.264, H.265, VP8, VP9, AV1, JPEG
- 其他: MPEG-2 TS, MPEG-4等

## 核心数据结构

### RTP Header

```c
typedef struct _rtp_header_t
{
    uint32_t v:2;        // protocol version (2)
    uint32_t p:1;        // padding flag
    uint32_t x:1;        // header extension flag
    uint32_t cc:4;       // CSRC count
    uint32_t m:1;        // marker bit
    uint32_t pt:7;       // payload type
    uint32_t seq:16;     // sequence number
    uint32_t timestamp;  // timestamp
    uint32_t ssrc;       // synchronization source
} rtp_header_t;
```

**字段说明**:
- `v`: RTP版本，必须为2
- `p`: padding标志，指示包尾是否有填充字节
- `x`: extension标志，指示是否有扩展头
- `cc`: CSRC数量(0-15)
- `m`: marker位，具体含义由payload类型定义
- `pt`: payload类型(0-127)
- `seq`: 序列号，用于检测丢包和重排序
- `timestamp`: RTP时间戳，用于媒体同步
- `ssrc`: 同步源标识符，标识RTP流

### RTP Packet

```c
struct rtp_packet_t
{
    rtp_header_t rtp;
    uint32_t csrc[16];
    const void* extension;    // extension (valid only if rtp.x = 1)
    uint16_t extlen;          // extension length in bytes
    uint16_t extprofile;      // extension reserved
    const void* payload;      // payload
    int payloadlen;           // payload length in bytes
};
```

### RTCP Header

```c
typedef struct _rtcp_header_t
{
    uint32_t v:2;        // version
    uint32_t p:1;        // padding
    uint32_t rc:5;       // reception report count
    uint32_t pt:8;       // packet type
    uint32_t length:16;  // pkt len in words, w/o this word
} rtcp_header_t;
```

### RTCP SR (Sender Report)

```c
typedef struct _rtcp_sr_t
{
    uint32_t ssrc;
    uint32_t ntpmsw;   // ntp timestamp MSW (in second)
    uint32_t ntplsw;   // ntp timestamp LSW (in picosecond)
    uint32_t rtpts;    // rtp timestamp
    uint32_t spc;      // sender packet count
    uint32_t soc;      // sender octet count
} rtcp_sr_t;
```

### RTCP RB (Report Block)

```c
typedef struct _rtcp_rb_t
{
    uint32_t ssrc;
    uint32_t fraction:8;      // fraction lost
    uint32_t cumulative:24;   // cumulative number of packets lost
    uint32_t exthsn;          // extended highest sequence number received
    uint32_t jitter;          // interarrival jitter
    uint32_t lsr;             // last SR
    uint32_t dlsr;            // delay since last SR
} rtcp_rb_t;
```

### RTP Member

```c
struct rtp_member
{
    int32_t ref;

    uint32_t ssrc;                // ssrc
    rtcp_sr_t rtcp_sr;
    rtcp_sdes_item_t sdes[9];     // SDES items

    uint64_t rtcp_clock;          // last RTCP SR/RR packet clock

    uint16_t rtp_seq;             // last RTP packet sequence
    uint32_t rtp_timestamp;       // last RTP packet timestamp
    uint64_t rtp_clock;           // last RTP packet clock
    uint32_t rtp_packets;         // RTP packet count
    uint64_t rtp_bytes;           // RTP octet count

    double jitter;
    uint32_t rtp_packets0;        // last SR received RTP packets
    uint32_t rtp_expected0;       // last SR expect RTP sequence number

    uint16_t rtp_probation;
    uint16_t rtp_seq_base;        // init sequence number
    uint32_t rtp_seq_bad;         // bad sequence number
    uint32_t rtp_seq_cycles;      // high extension sequence number
};
```

**关键字段**:
- `ssrc`: 同步源标识符
- `rtcp_sr`: 发送方报告信息
- `sdes`: 源描述信息(CNAME, NAME等)
- `rtp_seq`: 最后收到的RTP序列号
- `rtp_timestamp`: 最后收到的RTP时间戳
- `jitter`: 抖动统计
- `rtp_seq_cycles`: 序列号回绕次数

### RTP Context

```c
struct rtp_context
{
    struct rtp_event_t handler;
    void* cbparam;

    void *members;      // rtp source list
    void *senders;      // rtp sender list
    struct rtp_member *self;

    // RTP/RTCP
    int avg_rtcp_size;
    int rtcp_bw;
    int rtcp_cycle;     // for RTCP SDES
    int frequence;
    int init;
    int role;           // 1-sender, 0-receiver
};
```

**关键字段**:
- `handler`: 事件回调函数
- `members`: 所有会话成员列表
- `senders`: 发送方成员列表
- `self`: 本地成员信息
- `role`: 角色(1-发送方, 0-接收方)
- `rtcp_bw`: RTCP带宽(字节)
- `frequence`: RTP频率(时钟频率)

## 核心 API

### RTP 会话管理

#### 创建 RTP 会话

```c
void* rtp_create(struct rtp_event_t *handler,
                 void* param,
                 uint32_t ssrc,
                 uint32_t timestamp,
                 int frequence,
                 int bandwidth,
                 int sender);
```

**参数**:
- `handler`: 事件回调函数
- `param`: 用户参数
- `ssrc`: RTP SSRC
- `timestamp`: 基础时间戳
- `frequence`: RTP频率(如音频8000, 视频90000)
- `bandwidth`: 带宽(字节)
- `sender`: 1-发送方(SR), 0-接收方(RR)

**返回**: RTP对象句柄

#### 销毁 RTP 会话

```c
int rtp_destroy(void* rtp);
```

#### 事件回调

```c
struct rtp_event_t
{
    void (*on_rtcp)(void* param, const struct rtcp_msg_t* msg);
};
```

**回调函数**:
- `on_rtcp`: 接收到RTCP消息时的回调

### RTP 包处理

#### RTP 发送通知

```c
int rtp_onsend(void* rtp, const void* data, int bytes);
```

**功能**: 通知RTP层一个RTP包已发送

**参数**:
- `rtp`: RTP对象
- `data`: RTP包(包含RTP头)
- `bytes`: RTP包大小

**返回**: 0-成功, <0-错误

#### RTP 接收通知

```c
int rtp_onreceived(void* rtp, const void* data, int bytes);
```

**功能**: 通知RTP层接收到一个RTP包

**参数**:
- `rtp`: RTP对象
- `data`: RTP包(包含RTP头)
- `bytes`: RTP包大小

**返回**:
- 1: 正常
- 0: RTP包正常但序列号乱序
- <0: 错误

#### RTCP 接收通知

```c
int rtp_onreceived_rtcp(void* rtp, const void* rtcp, int bytes);
```

**功能**: 接收RTCP包

**参数**:
- `rtp`: RTP对象
- `rtcp`: RTCP包(包含RTCP头)
- `bytes`: RTCP包大小

**返回**: 0-成功, <0-错误

### RTCP 报告生成

#### 创建 RTCP Report (SR/RR)

```c
int rtp_rtcp_report(void* rtp, void* rtcp, int bytes);
```

**功能**: 生成RTCP SR(发送方)或RR(接收方)报告

**参数**:
- `rtp`: RTP对象
- `rtcp`: RTCP缓冲区
- `bytes`: 缓冲区大小

**返回**: >0-RTCP包大小, 0-错误

#### 创建 RTCP BYE

```c
int rtp_rtcp_bye(void* rtp, void* rtcp, int bytes);
```

**功能**: 生成RTCP BYE包，通知会话结束

#### 创建 RTCP APP

```c
int rtp_rtcp_app(void* rtp, void* rtcp, int bytes,
                 const char name[4], const void* app, int len);
```

**功能**: 生成应用特定的RTCP包

#### 创建 RTCP RTPFB

```c
int rtp_rtcp_rtpfb(void* rtp, void* data, int bytes,
                   enum rtcp_rtpfb_type_t id,
                   const rtcp_rtpfb_t *rtpfb);
```

**功能**: 生成RTP反馈包(如NACK)

**RTPFB 类型**:
- `RTCP_RTPFB_NACK`: Generic NACK
- `RTCP_RTPFB_TMMBR`: Temporary Maximum Media Stream Bit Rate Request
- `RTCP_RTPFB_TMMBN`: Temporary Maximum Media Stream Bit Rate Notification
- `RTCP_RTPFB_CCFB`: RTP Congestion Control Feedback

#### 创建 RTCP PSFB

```c
int rtp_rtcp_psfb(void* rtp, void* data, int bytes,
                  enum rtcp_psfb_type_t id,
                  const rtcp_psfb_t* psfb);
```

**功能**: 生成Payload特定反馈包

**PSFB 类型**:
- `RTCP_PSFB_PLI`: Picture Loss Indication (请求关键帧)
- `RTCP_PSFB_SLI`: Slice Loss Indication
- `RTCP_PSFB_FIR`: Full Intra Request (请求完整帧)
- `RTCP_PSFB_REMB`: Receiver Estimated Maximum Bitrate

### RTP Packet 序列化/反序列化

#### 反序列化 RTP 包

```c
int rtp_packet_deserialize(struct rtp_packet_t *pkt,
                           const void* data,
                           int bytes);
```

**功能**: 将字节流解析为RTP包结构

**参数**:
- `pkt`: RTP包结构[out]
- `data`: RTP字节流
- `bytes`: 字节流长度

**返回**: 0-成功, <0-错误

#### 序列化 RTP 包

```c
int rtp_packet_serialize(const struct rtp_packet_t *pkt,
                        void* data,
                        int bytes);
```

**功能**: 将RTP包结构序列化为字节流

**参数**:
- `pkt`: RTP包结构
- `data`: 输出缓冲区[out]
- `bytes`: 缓冲区大小

**返回**: >0-RTP包大小, <0-错误, =0-不可能

### RTP Payload 编解码

#### Payload 回调函数

```c
struct rtp_payload_t
{
    void* (*alloc)(void* param, int bytes);
    void (*free)(void* param, void *packet);
    int (*packet)(void* param, const void *packet, int bytes,
                  uint32_t timestamp, int flags);
};
```

**回调函数**:
- `alloc`: 分配内存
- `free`: 释放内存
- `packet`: payload包回调(完整的媒体帧)

#### 创建 Payload Encoder

```c
void* rtp_payload_encode_create(int payload,
                                const char* name,
                                uint16_t seq,
                                uint32_t ssrc,
                                struct rtp_payload_t *handler,
                                void* cbparam);
```

**功能**: 创建RTP payload编码器

**参数**:
- `payload`: RTP payload type (0-127)
- `name`: RTP payload名称
- `seq`: RTP序列号
- `ssrc`: RTP SSRC
- `handler`: 用户回调函数
- `cbparam`: 用户参数

**返回**: 编码器句柄

#### 销毁 Payload Encoder

```c
void rtp_payload_encode_destroy(void* encoder);
```

#### Payload 编码输入

```c
int rtp_payload_encode_input(void* encoder,
                             const void* data,
                             int bytes,
                             uint32_t timestamp);
```

**功能**: 输入媒体数据进行RTP封装

**参数**:
- `encoder`: 编码器句柄
- `data`: 媒体数据
- `bytes`: 数据长度
- `timestamp`: RTP时间戳

**返回**: 0-成功, <0-失败

#### 创建 Payload Decoder

```c
void* rtp_payload_decode_create(int payload,
                                const char* name,
                                struct rtp_payload_t *handler,
                                void* cbparam);
```

**功能**: 创建RTP payload解码器

#### 销毁 Payload Decoder

```c
void rtp_payload_decode_destroy(void* decoder);
```

#### Payload 解码输入

```c
int rtp_payload_decode_input(void* decoder,
                             const void* packet,
                             int bytes);
```

**功能**: 输入RTP包进行解码

**参数**:
- `decoder`: 解码器句柄
- `packet`: RTP包(包含RTP头)
- `bytes`: RTP包长度

**返回**: 1-已处理, 0-丢弃, <0-失败

### RTP Profile

#### 查找 Payload Profile

```c
const struct rtp_profile_t* rtp_profile_find(int payload);
```

**功能**: 根据payload type查找profile信息

**返回**: Profile信息，NULL表示不存在

**Profile 结构**:
```c
struct rtp_profile_t
{
    int payload;     // 0~127
    int avtype;      // 0-unknown, 1-audio, 2-video, 3-system
    int channels;    // number of channels
    int frequency;   // clock rate
    char name[32];   // case insensitive
};
```

**常用 Payload 类型**:
- 音频: `RTP_PAYLOAD_PCMU(0)`, `RTP_PAYLOAD_PCMA(8)`, `RTP_PAYLOAD_G722(9)`, `RTP_PAYLOAD_OPUS(103)`
- 视频: `RTP_PAYLOAD_H264(98)`, `RTP_PAYLOAD_H265(100)`, `RTP_PAYLOAD_VP8(105)`, `RTP_PAYLOAD_VP9(106)`

### 辅助函数

#### RTCP 间隔计算

```c
int rtp_rtcp_interval(void* rtp);
```

**功能**: 计算下次发送RTCP报告的时间间隔(毫秒)

**返回**: 间隔时间(毫秒)

#### SDES 信息管理

```c
const char* rtp_get_cname(void* rtp, uint32_t ssrc);
const char* rtp_get_name(void* rtp, uint32_t ssrc);
int rtp_set_info(void* rtp, const char* cname, const char* name);
```

**功能**: 获取/设置SDES信息(CNAME, NAME)

## 使用示例

### 创建 RTP 发送方

```c
#include "rtp.h"
#include "rtp-payload.h"

// RTCP事件回调
static void on_rtcp(void* param, const struct rtcp_msg_t* msg)
{
    if (msg->type == RTCP_RR) {
        // 接收到接收方报告
        printf("Received RR from SSRC: %u\n", msg->ssrc);
        printf("  Fraction lost: %u\n", msg->u.rr.fraction);
        printf("  Cumulative lost: %u\n", msg->u.rr.cumulative);
        printf("  Jitter: %u\n", msg->u.rr.jitter);
    }
}

// 创建RTP会话
struct rtp_event_t handler = {
    .on_rtcp = on_rtcp
};

void* rtp = rtp_create(&handler, NULL,
                       0x12345678,  // SSRC
                       0,           // timestamp
                       90000,       // frequence (90kHz for video)
                       1000000,     // bandwidth (1Mbps)
                       1);          // sender

// 设置SDES信息
rtp_set_info(rtp, "user@host", "User Name");
```

### RTP 包发送

```c
// 发送RTP包
uint8_t rtp_packet[1500];
int packet_size = /* 构建RTP包 */;

// 通知RTP层
rtp_onsend(rtp, rtp_packet, packet_size);

// 实际发送(通过socket等)
sendto(sock, rtp_packet, packet_size, ...);
```

### RTP 包接收

```c
// 接收RTP包
uint8_t buffer[2048];
int r = recvfrom(sock, buffer, sizeof(buffer), ...);

// 通知RTP层
rtp_onreceived(rtp, buffer, r);

// 解析RTP包
struct rtp_packet_t pkt;
if (0 == rtp_packet_deserialize(&pkt, buffer, r)) {
    printf("RTP packet: seq=%u, ts=%u, pt=%u\n",
           pkt.rtp.seq, pkt.rtp.timestamp, pkt.rtp.pt);

    // 处理payload
    process_payload(pkt.payload, pkt.payloadlen);
}
```

### RTCP 报告发送

```c
// 定期发送RTCP报告
int interval = rtp_rtcp_interval(rtp);  // 获取发送间隔

// 等待interval毫秒后...

uint8_t rtcp_buffer[1024];
int rtcp_size = rtp_rtcp_report(rtp, rtcp_buffer, sizeof(rtcp_buffer));

if (rtcp_size > 0) {
    // 发送RTCP报告
    sendto(sock, rtcp_buffer, rtcp_size, ...);
}
```

### 使用 Payload Encoder

```c
#include "rtp-payload.h"

// Payload回调
static void* payload_alloc(void* param, int bytes)
{
    return malloc(bytes);
}

static void payload_free(void* param, void *packet)
{
    free(packet);
}

static int payload_packet(void* param, const void *packet,
                         int bytes, uint32_t timestamp, int flags)
{
    // RTP包已封装好，发送它
    sendto(sock, packet, bytes, ...);
    return 0;
}

// 创建H.264编码器
struct rtp_payload_t payload_handler = {
    .alloc = payload_alloc,
    .free = payload_free,
    .packet = payload_packet
};

void* encoder = rtp_payload_encode_create(
    RTP_PAYLOAD_H264,      // payload type
    "H264",                // payload name
    1000,                  // initial seq
    0x12345678,            // SSRC
    &payload_handler,
    NULL);

// 输入H.264帧
uint8_t h264_frame[10000];
int frame_size = /* 读取H.264帧 */;

rtp_payload_encode_input(encoder, h264_frame, frame_size, timestamp);

// 清理
rtp_payload_encode_destroy(encoder);
```

### 使用 Payload Decoder

```c
// Payload回调
static int on_frame(void* param, const void *packet,
                   int bytes, uint32_t timestamp, int flags)
{
    // 完整的H.264帧
    if (flags & RTP_PAYLOAD_FLAG_PACKET_LOST) {
        printf("Warning: packet lost before this frame\n");
    }

    // 处理完整帧
    decode_h264(packet, bytes, timestamp);
    return 0;
}

// 创建H.264解码器
struct rtp_payload_t payload_handler = {
    .alloc = payload_alloc,
    .free = payload_free,
    .packet = on_frame
};

void* decoder = rtp_payload_decode_create(
    RTP_PAYLOAD_H264,
    "H264",
    &payload_handler,
    NULL);

// 输入RTP包
uint8_t rtp_packet[1500];
int packet_size = recvfrom(sock, rtp_packet, sizeof(rtp_packet), ...);

rtp_payload_decode_input(decoder, rtp_packet, packet_size);

// 清理
rtp_payload_decode_destroy(decoder);
```

### 发送 NACK (丢包重传请求)

```c
// 检测到丢包，发送NACK
rtcp_rtpfb_t rtpfb;
rtcp_nack_t nack;

nack.pid = 100;  // 丢失的包序列号
nack.blp = 0;    // bitmask of following lost packets

rtpfb.media = remote_ssrc;
rtpfb.u.nack.nack = &nack;
rtpfb.u.nack.count = 1;

uint8_t rtcp_buffer[1024];
int size = rtp_rtcp_rtpfb(rtp, rtcp_buffer, sizeof(rtcp_buffer),
                          RTCP_RTPFB_NACK, &rtpfb);

if (size > 0) {
    sendto(sock, rtcp_buffer, size, ...);
}
```

### 发送 PLI (请求关键帧)

```c
// 请求关键帧
rtcp_psfb_t psfb;
psfb.media = remote_ssrc;

uint8_t rtcp_buffer[1024];
int size = rtp_rtcp_psfb(rtp, rtcp_buffer, sizeof(rtcp_buffer),
                         RTCP_PSFB_PLI, &psfb);

if (size > 0) {
    sendto(sock, rtcp_buffer, size, ...);
}
```

## 关键参数说明

### RTCP 带宽分配

根据 RFC 3550:
- RTCP 带宽占总会话带宽的 5%
- 其中 25% 分配给发送方
- 75% 分配给接收方

```c
#define RTCP_BANDWIDTH_FRACTION         0.05
#define RTCP_SENDER_BANDWIDTH_FRACTION  0.25

// 计算RTCP带宽
int session_bandwidth = 1000000;  // 1Mbps
int rtcp_bw = session_bandwidth * RTCP_BANDWIDTH_FRACTION;  // 50kbps
```

### RTCP 报告间隔

```c
#define RTCP_REPORT_INTERVAL      5000  // 5秒
#define RTCP_REPORT_INTERVAL_MIN  2500  // 最小2.5秒
```

### RTP 序列号管理

```c
#define RTP_DROPOUT    500   // 序列号跳变阈值
#define RTP_MISORDER   100   // 乱序阈值
#define RTP_PROBATION  2     // 序列号验证次数
```

## 时钟频率

不同媒体类型使用不同的时钟频率:

| 媒体类型 | 频率 | 说明 |
|---------|------|------|
| 音频(8kHz采样) | 8000 | G.711, G.729等 |
| 音频(16kHz采样) | 16000 | G.722等 |
| 音频(48kHz采样) | 48000 | Opus, AAC等 |
| 视频 | 90000 | H.264, H.265, VP8, VP9等 |

**时间戳计算示例**:
```c
// 音频: 每20ms一帧，采样率8000Hz
uint32_t audio_ts_increment = 8000 * 0.020;  // 160

// 视频: 每33.3ms一帧 (30fps)，时钟90kHz
uint32_t video_ts_increment = 90000 / 30;    // 3000
```

## 常见应用场景

### 1. 音频流传输

```c
// 创建音频RTP会话 (PCMU)
void* rtp = rtp_create(&handler, NULL, ssrc, 0, 8000, 64000, 1);

// 创建PCMU编码器
void* encoder = rtp_payload_encode_create(
    RTP_PAYLOAD_PCMU, "PCMU", seq, ssrc, &payload_handler, NULL);

// 每20ms发送一个音频帧 (160字节)
while (running) {
    uint8_t audio_data[160];
    read_audio(audio_data, 160);

    rtp_payload_encode_input(encoder, audio_data, 160, timestamp);
    timestamp += 160;  // 增加20ms

    usleep(20000);  // 等待20ms
}
```

### 2. 视频流传输

```c
// 创建视频RTP会话 (H.264)
void* rtp = rtp_create(&handler, NULL, ssrc, 0, 90000, 2000000, 1);

// 创建H.264编码器
void* encoder = rtp_payload_encode_create(
    RTP_PAYLOAD_H264, "H264", seq, ssrc, &payload_handler, NULL);

// 发送视频帧
while (running) {
    uint8_t h264_frame[100000];
    int frame_size = read_h264_frame(h264_frame);

    rtp_payload_encode_input(encoder, h264_frame, frame_size, timestamp);
    timestamp += 3000;  // 增加33.3ms (30fps)

    usleep(33333);  // 等待33.3ms
}
```

### 3. 接收并解码

```c
// 创建RTP接收会话
void* rtp = rtp_create(&handler, NULL, ssrc, 0, 90000, 2000000, 0);

// 创建H.264解码器
void* decoder = rtp_payload_decode_create(
    RTP_PAYLOAD_H264, "H264", &payload_handler, NULL);

// 接收循环
while (running) {
    uint8_t buffer[2048];
    int r = recvfrom(sock, buffer, sizeof(buffer), ...);

    // 判断是RTP还是RTCP
    uint8_t pt = (buffer[1] & 0x7F);
    if (pt >= 64 && pt <= 95) {
        // RTCP
        rtp_onreceived_rtcp(rtp, buffer, r);
    } else {
        // RTP
        rtp_onreceived(rtp, buffer, r);
        rtp_payload_decode_input(decoder, buffer, r);
    }
}
```

## 总结

**librtp 核心功能**:
1. **RTP/RTCP 协议实现**: 完整的 RFC 3550 实现
2. **序列号管理**: 丢包检测、乱序处理
3. **时间戳管理**: 抖动计算、媒体同步
4. **RTCP 报告**: SR, RR, SDES, BYE等
5. **Payload 封装**: 支持多种音视频编解码器
6. **扩展功能**: NACK, PLI, FIR等反馈机制

**使用建议**:
1. 正确设置时钟频率(音频8000/16000/48000, 视频90000)
2. 定期发送RTCP报告(使用`rtp_rtcp_interval()`计算间隔)
3. 设置SDES信息(CNAME必须唯一)
4. 处理RTCP反馈(RR, NACK, PLI等)
5. 使用payload编解码器简化RTP封装/解封装

**注意事项**:
1. RTP/RTCP通常使用不同端口(如5000/5001)
2. SSRC必须在会话中唯一
3. 时间戳必须单调递增
4. RTCP带宽不应超过会话带宽的5%
5. 接收方应及时发送RR报告质量
