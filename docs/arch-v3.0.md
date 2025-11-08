# lwsip v3.0 架构设计

## 设计定位

lwsip = **Light-Weight SIP stack for RTOS**

### 核心定位

lwsip 是一个**拿来即用的完整 SIP 客户端框架**，专为嵌入式系统和 RTOS 环境设计。

**与基础协议库的关系**：
- **基础库**（libsip、libice、librtp）：专注协议本身，只处理协议相关的逻辑
- **lwsip**：在基础库之上构建完整应用框架，补齐协议之外的所有必要组件

### 设计目标

让用户能够**快速集成 SIP 功能**，只需三步：

1. **创建线程**：根据应用需求选择单线程或多线程模式
2. **调用接口**：使用 lwsip 提供的高层 API（`lws_agent_loop()`, `lws_sess_loop()`, `lws_trans_loop()`）
3. **适配媒体**：对接音视频设备或文件

完成以上三步，整个 SIP 客户端系统即可运行。

### lwsip 补齐的功能

基础协议库专注于协议处理，lwsip 在此之上补齐了以下功能：

| 功能模块 | 基础库提供 | lwsip 补齐 |
|---------|-----------|-----------|
| **SIP 协议处理** | ✓ (libsip) | 高层封装 |
| **ICE NAT 穿透** | ✓ (libice) | 高层封装 |
| **RTP 媒体流** | ✓ (librtp) | 高层封装 |
| **统一传输层** | ✗ | ✓ lws_trans (UDP/TCP/MQTT) |
| **设备抽象层** | ✗ | ✓ lws_dev (音视频/文件) |
| **会话协调层** | ✗ | ✓ lws_sess (ICE+RTP+Dev) |
| **定时器管理** | ✗ | ✓ 集成到 loop 函数 |
| **线程安全设计** | ✗ | ✓ 多线程协调机制 |
| **SDP 自动生成** | ✗ | ✓ 自动组装 SDP |
| **完整呼叫流程** | ✗ | ✓ Agent + Session 协调 |

### 目标用户和使用场景

**目标用户**：
- 嵌入式系统开发者（需要快速集成 SIP 功能）
- RTOS 应用开发者（FreeRTOS、Zephyr、RT-Thread）
- IoT 设备开发者（资源受限环境）
- VoIP 终端开发者（IP 电话、对讲机）

**典型场景**：
- IP 可视对讲门禁
- 智能音箱/语音助手（SIP 语音通话）
- 监控摄像头（SIP 视频流）
- 工业对讲系统
- 车载通信终端

---

## 设计原则

lwsip v3.0 遵循以下核心设计原则：

### 1. 无内部线程设计

**所有模块均不创建内部线程**，由应用层决定线程模型。

- **原因**：适配各种 RTOS 和裸机环境
- **实现**：通过 `lws_xxx_loop()` 函数由应用层驱动
- **灵活性**：支持单线程到多线程的任意配置

### 2. 事件驱动架构

使用**轮询和回调**机制，避免阻塞等待。

- **轮询**：`lws_xxx_loop(timeout)` 处理网络 I/O 和定时器
- **回调**：通过回调函数通知应用层状态变化和数据到达
- **非阻塞**：所有 API 调用均为非阻塞，不会长时间占用 CPU

### 3. 清晰的分层设计

**五层架构**，每层职责明确：

1. **应用层**：用户代码，创建线程和调用 loop 函数
2. **协调层**：lws_agent（SIP 信令）+ lws_sess（媒体会话）
3. **协议层**：lws_ice + lws_rtp（基于 libice/librtp）
4. **设备层**：lws_dev（音视频设备抽象）
5. **传输层**：lws_trans（统一网络传输）

### 4. RTOS 友好设计

**最小资源占用**，支持资源受限环境：

- **最小内存**：单线程模式堆栈只需 8-16KB
- **静态分配**：支持完全静态内存分配（可选）
- **无动态线程**：不创建内部线程，无线程创建开销
- **可裁剪**：各模块可独立编译，按需裁剪

### 5. 开箱即用

**高层 API 设计**，降低使用门槛：

- **loop 函数**：集成定时器、网络 I/O、状态机处理
- **自动化流程**：SDP 自动生成、ICE 自动协商、RTCP 自动发送
- **完整示例**：提供端到端的示例代码
- **清晰文档**：详细的 API 文档和集成指南

---

## 架构总览

```
┌─────────────────────────────────────────────────────────┐
│                      应用层 (Application)                │
│                                                          │
│  用户只需：                                               │
│  1. 创建线程（可选单/双/三线程）                           │
│  2. 调用 lws_agent_loop()   (SIP 信令)                  │
│  3. 调用 lws_sess_loop()    (媒体发送)                  │
│  4. 调用 lws_trans_loop()   (网络接收)                  │
│  5. 适配音视频设备或文件                                  │
│                                                          │
└─────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────┐
│                    lwsip 核心层                          │
│              (补齐基础库之外的所有功能)                    │
│                                                          │
│  ┌──────────────┐                  ┌────────────────┐  │
│  │  lws_agent   │◄────── SDP ─────►│   lws_sess     │  │
│  │ (SIP 信令)   │                  │ (媒体会话协调)  │  │
│  │              │                  │  ├─ lws_ice    │  │
│  │ - 注册/呼叫  │                  │  ├─ lws_rtp    │  │
│  │ - 定时器集成 │                  │  └─ lws_dev    │  │
│  │ - 状态管理   │                  │                 │  │
│  └──────┬───────┘                  └────────┬────────┘  │
│         │                                   │           │
│         │          ┌────────────────┐       │           │
│         └─────────►│   lws_trans    │◄──────┘           │
│                    │  (统一传输层)   │                   │
│                    │  - Socket      │                   │
│                    │  - MQTT        │                   │
│                    │  - 数据包分发   │                   │
│                    │  - 线程安全     │                   │
│                    └────────────────┘                   │
└─────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────┐
│                  底层协议库 (3rds)                        │
│                  (专注协议本身)                           │
│                                                          │
│  - libsip  (SIP 协议处理)                                │
│  - libice  (ICE/STUN/TURN NAT 穿透)                     │
│  - librtp  (RTP/RTCP 媒体流)                            │
└─────────────────────────────────────────────────────────┘
```

---

## 模块设计

### lws_agent 模块 (SIP 信令层)

**定位**：SIP 协议层的高层封装，提供开箱即用的 SIP 功能。

**线程模型**：无内部线程，通过 `lws_agent_loop()` 驱动

**核心职责**：
- SIP 注册、呼叫建立、挂断等信令处理
- SDP Offer/Answer 协商
- 定时器管理（SIP 事务定时器 Timer A-K）
- UAC/UAS 状态机管理
- 通过回调通知应用层呼叫状态变化

**补齐的功能**（相比 libsip）：
- ✅ 定时器集成到 `lws_agent_loop()` 中，应用层无需手动管理
- ✅ SDP 自动生成和解析（与 lws_sess 协调）
- ✅ 高层 API：`lws_agent_make_call()`, `lws_agent_answer_call()` 等
- ✅ 统一的传输层抽象（通过 lws_trans）

**关键设计**：
- `lws_agent_loop()` 内部处理：
  1. 调用 `lws_trans_recv()` 接收 SIP 消息
  2. 调用底层 `sip_agent_input()` 输入到 libsip
  3. 检查定时器队列，触发到期的 SIP 定时器
  4. 触发用户注册的回调函数
- 传输层事件和定时器事件统一处理

**API 示例**：
```c
// 创建 SIP Agent
lws_agent_t* agent = lws_agent_create(&config, &handler);
lws_agent_start(agent);  // 开始注册

// 在线程中驱动
while (running) {
    lws_agent_loop(agent, 100);  // 100ms 超时
}

// 发起呼叫（SDP 由 lws_sess 提供）
lws_dialog_t* dialog = lws_agent_make_call(agent, "sip:bob@example.com", local_sdp);
```

---

### lws_sess 模块 (媒体会话协调层)

**定位**：媒体会话的高层抽象，协调 ICE、RTP、Dev 三层，提供端到端的媒体连接管理。

**这是 lwsip 的核心创新**：基础库只提供 ICE、RTP 的协议处理，lws_sess 补齐了完整的媒体会话流程。

**线程模型**：无内部线程，通过 `lws_sess_loop()` 驱动媒体发送流程

**核心职责**：
- **ICE 流程协调**：candidate 收集 → 连接性检查 → 选择最优路径
- **RTP 会话管理**：RTP 打包/解包、RTCP 定时发送
- **设备协调**：从 Dev 层采集数据 → 发送；接收数据 → 送 Dev 播放
- **会话状态管理**：IDLE → GATHERING → CONNECTING → CONNECTED

**补齐的功能**（相比基础库）：
- ✅ ICE + RTP + Dev 三层协调（基础库需应用层自己协调）
- ✅ SDP 自动生成（包含 ICE candidates 和 RTP 编解码信息）
- ✅ 定时器集成（ICE keepalive、RTCP 发送）
- ✅ 媒体流自动驱动（采集 → 打包 → 发送）
- ✅ 抖动缓冲区（播放端平滑抖动）

**数据流**：

**发送流程**（主动驱动）：
```
lws_sess_loop() (20ms 调用一次)
  ↓
lws_dev_read_audio(dev, buf, samples)  // 采集 20ms 音频
  ↓
lws_rtp_send_audio(rtp, buf, samples, timestamp)  // RTP 打包
  ↓
lws_ice_send(ice, rtp_packet, len)  // 通过 ICE 发送
  ↓
lws_trans_send(trans, packet, len, addr, port)  // 网络发送
  ↓
网络
```

**接收流程**（被动驱动）：
```
网络
  ↓
lws_trans_loop() (接收网络数据)
  ↓
数据包分发器 (区分 SIP/STUN/RTP)
  ↓
lws_ice_input(ice, packet, len)  // 处理 STUN 或 RTP
  ↓
lws_rtp_input(rtp, data, len)  // RTP 解包
  ↓
on_audio_frame(data, samples) 回调
  ↓
lws_dev_write_audio(dev, data, samples)  // 播放
```

**API 示例**：
```c
// 创建媒体会话
lws_sess_t* session = lws_sess_create(&config, &handler);

// 收集 ICE candidates（异步）
lws_sess_gather_candidates(session);

// SDP 就绪回调
void on_sdp_ready(lws_sess_t* sess, const char* sdp, void* userdata) {
    // SDP 已生成，可以发送给对端
    lws_agent_make_call(agent, target_uri, sdp);
}

// 设置远端 SDP
lws_sess_set_remote_sdp(session, remote_sdp);
lws_sess_start_ice(session);  // 开始 ICE 连接

// 在线程中驱动媒体发送
while (running) {
    lws_sess_loop(session, 20);  // 20ms 驱动一次
}
```

---

### lws_ice 和 lws_rtp 模块 (协议处理层)

**定位**：基于 libice 和 librtp 的封装，保持协议处理的纯粹性。

**设计原则**：
- 保持与底层库一致的**被动输入模型**
- 通过 `lws_ice_input()` 和 `lws_rtp_input()` 接收数据
- 不创建内部线程，不自己驱动定时器

**lws_ice (ICE 协议层)**：

**职责**：
- Host/STUN/TURN candidate 收集
- ICE connectivity checks
- 选择最优 candidate pair
- 通过回调传递接收的数据

**特点**：
- 基于 libice 实现
- 被动输入模型，接收数据通过 `lws_ice_input()` 输入
- STUN/TURN 处理完全集成
- 连接建立后成为 RTP 的传输通道

**lws_rtp (RTP/RTCP 协议层)**：

**职责**：
- RTP 打包/解包
- RTCP 报告生成（SR/RR）
- 统计信息维护（丢包率、抖动等）
- 通过回调传递解包后的媒体帧

**特点**：
- 基于 librtp 实现
- 完全被动设计，输入通过 `lws_rtp_input()`
- 支持多种编解码（PCMU、PCMA、H.264 等）
- RTCP 定时器由 lws_sess 层管理

---

### lws_dev 模块 (设备抽象层)

**定位**：统一的音视频设备抽象，lwsip 补齐的重要功能之一。

**为什么需要 lws_dev**：
- 基础库只处理协议，不关心数据来源
- 应用层需要自己对接各种音视频设备（ALSA、PortAudio、V4L2 等）
- lws_dev 提供统一抽象，简化设备集成

**线程模型**：无内部线程，由应用层或 lws_sess 驱动采集/播放

**核心职责**：
- 音频设备采集（麦克风）和播放（扬声器）
- 视频设备采集（摄像头）和显示
- 文件读取/写入（WAV、MP4 等）
- 统一的设备抽象接口

**支持的设备类型**：
- `LWS_DEV_AUDIO_CAPTURE`: 音频采集（麦克风）
- `LWS_DEV_AUDIO_PLAYBACK`: 音频播放（扬声器）
- `LWS_DEV_VIDEO_CAPTURE`: 视频采集（摄像头）
- `LWS_DEV_VIDEO_DISPLAY`: 视频显示
- `LWS_DEV_FILE_READER`: 文件读取（用于测试或离线处理）
- `LWS_DEV_FILE_WRITER`: 文件写入（录音/录像）

**关键设计**：
- 提供统一的设备抽象，支持真实设备和文件
- 虚函数表设计，易于扩展新的设备驱动
- 支持同步和异步模式
- 时间戳同步机制（设备时间 → RTP 时间戳）

**API 示例**：
```c
// 创建音频采集设备
lws_dev_config_t dev_cfg;
lws_dev_init_audio_capture_config(&dev_cfg);
lws_dev_t* mic = lws_dev_create(&dev_cfg, &handler);
lws_dev_open(mic);
lws_dev_start(mic);

// 在 lws_sess_loop 中读取
char buf[320];  // 20ms @ 8kHz
int samples = lws_dev_read_audio(mic, buf, 160);
```

---

### lws_trans 模块 (统一传输层)

**定位**：统一的网络传输抽象，lwsip 补齐的关键功能。

**为什么需要 lws_trans**：
- libsip、libice 都需要网络传输，但各自接口不同
- 应用层需要自己管理 socket、处理 UDP/TCP 差异
- IoT 场景可能需要 MQTT 等非标准传输
- lws_trans 提供统一抽象，SIP 和 ICE 共用

**线程模型**：提供 `lws_trans_loop()` 用于接收数据，由应用层驱动

**核心职责**：
- 提供统一的传输抽象
- 支持多种传输协议（UDP/TCP/MQTT）
- 处理网络 I/O（非阻塞）
- 数据包分发（区分 SIP/STUN/RTP）
- 通过回调传递接收的数据

**实现类型**：
- `lws_trans_sock`: 基于 Socket 的传输（UDP/TCP/TLS）
- `lws_trans_mqtt`: 基于 MQTT 的传输（适合 NAT 穿透困难的 IoT 场景）
- 未来可扩展：WebSocket、QUIC 等

**关键设计**：
- **SIP 和 ICE 共用同一套传输层代码**，避免重复
- `lws_trans_loop()` 驱动数据接收流程
- 数据包分发器：根据协议特征（SIP/STUN/RTP）分发到不同模块
- 虚函数表设计，易于扩展新的传输类型
- 支持多传输实例（SIP 用一个 trans，ICE 用另一个）

**数据包分发机制**：
```c
int lws_trans_loop(lws_trans_t* trans, int timeout_ms) {
    // 1. 接收网络数据
    char buf[2048];
    int len = recv(trans->fd, buf, sizeof(buf), 0);

    // 2. 检测数据包类型
    if (is_sip_message(buf, len)) {
        // 分发给 SIP Agent
        trans->on_sip_data(trans, buf, len, addr, port);
    } else if (is_stun_packet(buf, len)) {
        // 分发给 ICE
        trans->on_ice_data(trans, buf, len, addr, port);
    } else if (is_rtp_packet(buf, len)) {
        // 分发给 RTP
        trans->on_ice_data(trans, buf, len, addr, port);  // 通过 ICE 传递
    }

    return processed;
}
```

**MQTT 传输设计**：
```
Topic 设计：
  - SIP 信令: lwsip/{realm}/{username}/signaling  (QoS 1)
  - ICE 候选: lwsip/{realm}/{session_id}/ice      (QoS 0)
  - RTP 媒体: lwsip/{realm}/{session_id}/rtp      (QoS 0)
```

**API 示例**：
```c
// 创建 UDP 传输
lws_trans_config_t trans_cfg;
lws_trans_init_udp_config(&trans_cfg, 0);  // 端口 0 = 自动分配
lws_trans_t* trans = lws_trans_create(&trans_cfg, &handler);
lws_trans_open(trans);

// 在线程中驱动接收
while (running) {
    lws_trans_loop(trans, 100);  // 100ms 超时
}
```

---

## 事件循环设计

### 核心三循环

应用层需要驱动三个主要循环：

#### 1. lws_agent_loop(timeout)

**功能**：SIP 信令处理

**内部流程**：
1. 处理 SIP 消息收发（通过 lws_trans）
2. 检查 SIP 定时器队列（Timer A-K）
3. 触发到期的定时器回调
4. 调用底层 libsip 的状态机
5. 触发用户回调（注册结果、呼叫状态变化等）

**调用频率**：建议 50-100ms

#### 2. lws_sess_loop(timeout)

**功能**：媒体发送驱动

**内部流程**：
1. 从 Dev 层采集音视频数据（`lws_dev_read_audio/video()`）
2. RTP 打包（`lws_rtp_send_audio/video()`）
3. 通过 ICE 发送到网络（`lws_ice_send()`）
4. 处理 RTCP 定时器（查询 `rtp_rtcp_interval()`，到期则发送 RTCP）
5. 更新媒体统计信息

**调用频率**：建议 10-20ms（音频帧间隔）

#### 3. lws_trans_loop(timeout)

**功能**：网络数据接收

**内部流程**：
1. 接收网络数据包（UDP/TCP/MQTT）
2. 检测数据包类型（SIP/STUN/RTP）
3. 分发给对应模块：
   - SIP 消息 → lws_agent
   - STUN 消息 → lws_ice
   - RTP 数据 → lws_ice → lws_rtp
4. 驱动 RTP 解包和 Dev 播放

**调用频率**：建议 10-50ms（实时性要求高）

### 数据流总览

**发送路径（主动驱动）**：
```
应用线程调用 lws_sess_loop()
  ↓
lws_dev_read_audio() / lws_dev_read_video()
  ↓
lws_rtp_send_audio() / lws_rtp_send_video()
  ↓
lws_ice_send()
  ↓
lws_trans_send()
  ↓
socket send() / MQTT publish()
  ↓
网络
```

**接收路径（事件驱动）**：
```
网络
  ↓
socket recv() / MQTT subscribe()
  ↓
应用线程调用 lws_trans_loop()
  ↓
数据包分发器 (SIP/STUN/RTP)
  ↓
lws_ice_input()  (处理 STUN 或转发 RTP)
  ↓
lws_rtp_input()
  ↓
on_audio_frame() / on_video_frame() 回调
  ↓
lws_dev_write_audio() / lws_dev_write_video()
```

---

## 线程模型

lwsip **不强制任何线程模型**，由应用层根据需求选择。以下是推荐的最佳实践：

### 单线程模式（适合 RTOS/嵌入式）

在主循环中依次调用三个 loop，实现完全的单线程运行。

**代码示例**：
```c
while (running) {
    lws_agent_loop(agent, 10);   // SIP 信令
    lws_sess_loop(session, 10);  // 媒体发送
    lws_trans_loop(trans, 10);   // 网络接收
}
```

**特点**：
- 最小资源占用（栈 8-16KB）
- 无线程切换开销
- 适合资源受限的嵌入式环境
- 简单易调试

**适用场景**：
- RTOS 单任务模式
- 裸机环境
- 低端 MCU（如 STM32F4）

---

### 双线程模式（常见配置）

**线程 1**：SIP 信令线程
```c
void sip_thread(void* arg) {
    while (running) {
        lws_agent_loop(agent, 100);  // 信令处理不需要太频繁
    }
}
```

**线程 2**：媒体线程
```c
void media_thread(void* arg) {
    while (running) {
        lws_sess_loop(session, 20);  // 媒体发送（20ms 音频帧）
        lws_trans_loop(trans, 10);   // 网络接收（实时性）
    }
}
```

**特点**：
- 信令和媒体分离
- 媒体不受信令处理影响
- 适合大多数应用场景
- 典型栈大小：每线程 32-64KB

**适用场景**：
- 普通 VoIP 终端
- IP 对讲机
- 智能音箱

---

### 三线程模式（高性能）

**线程 1**：SIP 信令线程
```c
void sip_thread(void* arg) {
    while (running) {
        lws_agent_loop(agent, 100);
    }
}
```

**线程 2**：媒体发送线程
```c
void media_send_thread(void* arg) {
    while (running) {
        lws_sess_loop(session, 20);  // 专注于采集和发送
    }
}
```

**线程 3**：媒体接收线程
```c
void media_recv_thread(void* arg) {
    while (running) {
        lws_trans_loop(trans, 10);  // 专注于接收和播放
    }
}
```

**特点**：
- 发送和接收完全分离
- 最高性能和实时性
- 适合高质量音视频应用
- 典型栈大小：每线程 64-128KB

**适用场景**：
- 高清视频监控
- 专业对讲系统
- 高端 VoIP 终端

---

### 线程安全设计

当多个线程同时访问 lwsip 时，需要考虑线程安全：

#### 1. 跨线程数据传递

使用**无锁队列**进行数据传递（推荐）：

```c
// lws_trans_loop (线程 3) 接收数据后投递到队列
lockfree_queue_push(agent_queue, packet);

// lws_agent_loop (线程 1) 从队列取数据
while (lockfree_queue_pop(agent_queue, &packet)) {
    lws_agent_input(agent, packet.data, packet.len);
}
```

#### 2. 回调线程上下文

**重要**：明确回调函数的调用线程上下文

| 回调函数 | 调用线程 | 注意事项 |
|---------|---------|---------|
| `on_register_result()` | lws_agent_loop() 所在线程 | 可直接操作 agent |
| `on_incoming_call()` | lws_trans_loop() 所在线程 | 需要跨线程通信 |
| `on_audio_frame()` | lws_trans_loop() 所在线程 | 可直接写入设备 |
| `on_sdp_ready()` | lws_sess_loop() 所在线程 | 可直接操作 session |

#### 3. 资源保护

如果多个线程共享资源，使用**细粒度锁**：

```c
// lws_trans 内部锁保护 fd
pthread_mutex_lock(&trans->lock);
send(trans->fd, data, len, 0);
pthread_mutex_unlock(&trans->lock);
```

---

## 完整呼叫流程示例

### 主叫方（UAC）完整流程

```c
// ========================================
// 步骤 1: 初始化传输层
// ========================================
lws_trans_config_t trans_cfg;
lws_trans_init_udp_config(&trans_cfg, 0);  // 自动分配端口

lws_trans_handler_t trans_handler = {
    .on_recv = on_trans_recv,
    .userdata = &app_ctx
};
lws_trans_t* trans = lws_trans_create(&trans_cfg, &trans_handler);
lws_trans_open(trans);

// ========================================
// 步骤 2: 创建 SIP Agent
// ========================================
lws_agent_config_t agent_cfg;
lws_agent_init_default_config(&agent_cfg, "alice", "password",
                               "sip.example.com", trans);

lws_agent_handler_t agent_handler = {
    .on_register_result = on_register_result,
    .on_dialog_state_changed = on_dialog_state_changed,
    .on_remote_sdp = on_remote_sdp,
    .userdata = &app_ctx
};
lws_agent_t* agent = lws_agent_create(&agent_cfg, &agent_handler);
lws_agent_start(agent);  // 开始注册

// ========================================
// 步骤 3: 创建媒体会话
// ========================================
lws_sess_config_t sess_cfg;
lws_sess_init_audio_config(&sess_cfg, "stun.example.com",
                            LWS_AUDIO_CODEC_PCMU);

// 设置音频设备
lws_dev_t* mic = create_audio_capture_device();
lws_dev_t* speaker = create_audio_playback_device();
sess_cfg.audio_capture_dev = mic;
sess_cfg.audio_playback_dev = speaker;

lws_sess_handler_t sess_handler = {
    .on_state_changed = on_sess_state_changed,
    .on_sdp_ready = on_sdp_ready,
    .on_connected = on_media_connected,
    .userdata = &app_ctx
};
lws_sess_t* session = lws_sess_create(&sess_cfg, &sess_handler);

// ========================================
// 步骤 4: 收集 ICE candidates
// ========================================
lws_sess_gather_candidates(session);

// ========================================
// 步骤 5: SDP 就绪后发起呼叫
// ========================================
void on_sdp_ready(lws_sess_t* sess, const char* sdp, void* userdata) {
    printf("本地 SDP 就绪，发起呼叫...\n");

    // 发起呼叫
    lws_dialog_t* dialog = lws_agent_make_call(agent,
                                                "sip:bob@example.com",
                                                sdp);
    app_ctx->dialog = dialog;
}

// ========================================
// 步骤 6: 收到远端 SDP
// ========================================
void on_remote_sdp(lws_agent_t* agent, lws_dialog_t* dialog,
                   const char* sdp, void* userdata) {
    printf("收到远端 SDP，开始 ICE 连接...\n");

    // 设置远端 SDP
    lws_sess_set_remote_sdp(session, sdp);

    // 开始 ICE 连接
    lws_sess_start_ice(session);
}

// ========================================
// 步骤 7: 媒体连接建立
// ========================================
void on_media_connected(lws_sess_t* sess, void* userdata) {
    printf("媒体已连接，开始传输！\n");
}

// ========================================
// 步骤 8: 主循环（单线程模式）
// ========================================
while (running) {
    lws_agent_loop(agent, 10);   // SIP 信令
    lws_sess_loop(session, 10);  // 媒体发送
    lws_trans_loop(trans, 10);   // 网络接收
}

// ========================================
// 步骤 9: 挂断
// ========================================
lws_agent_hangup(agent, dialog);

// ========================================
// 步骤 10: 清理
// ========================================
lws_sess_destroy(session);
lws_agent_destroy(agent);
lws_trans_destroy(trans);
lws_dev_destroy(mic);
lws_dev_destroy(speaker);
```

---

## 关键优势

### 1. 开箱即用

**用户只需三步**：
1. 创建线程（或单线程）
2. 调用 loop 函数
3. 适配音视频设备

**无需关心**：
- SIP 协议细节（事务、定时器、状态机）
- ICE NAT 穿透流程（candidate 收集、连接性检查）
- RTP 打包细节（序列号、时间戳、RTCP）
- 网络传输差异（UDP/TCP/MQTT）

### 2. 完整功能

lwsip 补齐了基础库之外的**所有必要功能**：

| 功能 | 传统方式（用户自己实现） | lwsip 提供 |
|-----|------------------------|-----------|
| SIP 定时器管理 | 自己创建定时器线程 | 集成到 loop |
| ICE+RTP 协调 | 自己编写协调逻辑 | lws_sess 自动协调 |
| 设备抽象 | 对接各种设备 API | lws_dev 统一接口 |
| 传输层抽象 | 管理多个 socket | lws_trans 统一抽象 |
| SDP 生成 | 手动拼接 SDP 字符串 | 自动生成 |
| 线程安全 | 自己加锁 | 内部已考虑 |

### 3. 灵活的线程模型

支持从**单线程到多线程**的任意配置：
- **单线程**：最小 8KB 栈，适合 RTOS
- **双线程**：信令 + 媒体，常见配置
- **三线程**：信令 + 发送 + 接收，高性能

### 4. 清晰的分层

五层架构，职责明确：
```
应用层 → 协调层(Agent/Sess) → 协议层(ICE/RTP) → 设备层(Dev) → 传输层(Trans)
```

### 5. 资源高效

- **最小内存**：单线程 8-16KB 栈
- **统一传输**：SIP 和 ICE 共用，无代码重复
- **可裁剪**：按需编译各模块

### 6. RTOS 友好

- **无内部线程**：不创建线程，不强制线程模型
- **无阻塞调用**：所有 API 非阻塞
- **静态分配**：支持完全静态内存分配（可选）

### 7. 易于扩展

- **虚函数表**：lws_trans、lws_dev 易于扩展新类型
- **模块化**：各模块独立，易于替换
- **清晰接口**：API 设计一致，易于理解

---

## 与基础库的关系

### 设计哲学对比

| 方面 | 基础库（libsip/libice/librtp） | lwsip |
|-----|------------------------------|-------|
| **定位** | 协议处理库 | 应用框架 |
| **职责** | 只处理协议本身 | 完整的端到端功能 |
| **输入模型** | 被动输入（xxx_input） | 主动循环（xxx_loop） |
| **定时器** | 应用层管理 | 集成到 loop |
| **传输层** | 需要应用层提供 | 内置统一传输层 |
| **设备层** | 不涉及 | 内置设备抽象 |
| **线程模型** | 完全线程无关 | 提供线程模型建议 |
| **使用复杂度** | 需要深入理解协议 | 开箱即用 |

### lwsip 如何使用基础库

```
lwsip 层                     基础库层
---------                   -----------
lws_agent_loop()
  ↓
  lws_trans_recv()  →  接收 SIP 消息
  ↓
  sip_agent_input()  ←  调用 libsip
  ↓
  检查定时器
  ↓
  sip_agent_ontimeout()  ←  调用 libsip
  ↓
  触发回调

lws_sess_loop()
  ↓
  lws_dev_read_audio()  →  采集音频
  ↓
  rtp_onsend()  ←  调用 librtp 打包
  ↓
  ice_agent_send()  ←  调用 libice 发送
  ↓
  lws_trans_send()  →  网络发送

lws_trans_loop()
  ↓
  lws_trans_recv()  →  接收网络数据
  ↓
  数据包分发
  ↓
  ice_agent_input()  ←  调用 libice
  ↓
  rtp_onreceived()  ←  调用 librtp 解包
  ↓
  lws_dev_write_audio()  →  播放音频
```

### 为什么不直接使用基础库？

**使用基础库直接开发的复杂度**：

1. 需要自己实现传输层（UDP/TCP socket 管理）
2. 需要自己管理定时器（SIP 事务定时器、RTCP 定时器）
3. 需要自己协调 ICE + RTP（何时开始 ICE、何时发送 RTP）
4. 需要自己对接设备（ALSA、PortAudio 等）
5. 需要自己生成 SDP（拼接 ICE candidates、RTP 编解码信息）
6. 需要自己处理线程同步（如果多线程）

**使用 lwsip 的简洁性**：

1. ✅ 传输层：内置 lws_trans，统一抽象
2. ✅ 定时器：集成到 loop 函数，自动管理
3. ✅ ICE+RTP 协调：lws_sess 自动协调
4. ✅ 设备对接：lws_dev 统一接口
5. ✅ SDP 生成：自动生成
6. ✅ 线程同步：内部已考虑

---

## 总结

### 核心价值

lwsip v3.0 = **基础协议库** + **完整应用框架**

**基础库提供**：协议处理（SIP/ICE/RTP）
**lwsip 补齐**：传输 + 设备 + 协调 + 定时器 + 线程安全

### 设计目标

让用户能够**快速集成 SIP 功能**，只需：
1. 创建线程
2. 调用 loop
3. 适配设备

### 适用场景

- 嵌入式 VoIP 终端
- IoT 音视频设备
- RTOS 应用
- 需要快速集成 SIP 的项目

### 关键特性

- ✅ 无内部线程设计
- ✅ 事件驱动架构
- ✅ 清晰的五层分层
- ✅ RTOS 友好（最小 8KB 栈）
- ✅ 开箱即用（loop 函数 + 自动化流程）
- ✅ 统一传输层（Socket/MQTT）
- ✅ 统一设备层（音视频/文件）
- ✅ 媒体会话协调（ICE+RTP+Dev）
- ✅ 灵活线程模型（1-3 线程）

### 与基础库的区别

| 基础库 | lwsip |
|-------|-------|
| 协议处理 | 应用框架 |
| 需要自己拼装 | 开箱即用 |
| 完全被动 | 主动循环 |
| 线程无关 | 线程模型建议 |
| 高灵活性 | 易用性优先 |

lwsip 在保持基础库灵活性的同时，提供了更高层次的抽象和完整的功能，让用户能够快速开发出可用的 SIP 客户端。
