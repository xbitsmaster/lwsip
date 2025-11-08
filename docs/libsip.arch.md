# libsip 架构与使用指南

## 概述

libsip 是 media-server 项目中的SIP协议栈实现，它实现了 RFC 3261 定义的 SIP (Session Initiation Protocol) 协议。libsip 提供了完整的 UAC (User Agent Client) 和 UAS (User Agent Server) 功能，支持建立、修改和终止多媒体会话。

**项目地址**: https://github.com/ireader/media-server

## 核心概念

### 1. Agent (sip_agent_t)

**定义**: Agent 是 libsip 的核心管理对象，负责协调所有 SIP 操作。

**结构**:
```c
struct sip_agent_t {
    int32_t ref;                      // 引用计数
    locker_t locker;                  // 线程锁
    struct list_head uac;             // UAC事务列表
    struct list_head uas;             // UAS事务列表
    struct sip_uas_handler_t handler; // UAS回调处理器
};
```

**作用**:
- 管理所有活动的 UAC/UAS 事务
- 提供事务的创建、查找和销毁功能
- 分发接收到的SIP消息到对应的事务处理器
- 维护事务生命周期

**关键API**:
- `sip_agent_create()`: 创建 agent，注册 UAS 回调处理器
- `sip_agent_destroy()`: 销毁 agent
- `sip_agent_input()`: 输入接收到的 SIP 消息（request/response）

**工作流程**:
1. 应用程序创建一个 agent 实例
2. Agent 内部维护两个事务列表：uac 和 uas
3. 当收到 SIP 消息时，通过 `sip_agent_input()` 分发：
   - 如果是 request，由 `sip_uas_input()` 处理，创建 UAS 事务
   - 如果是 response，由 `sip_uac_input()` 处理，匹配已有的 UAC 事务

---

### 2. Dialog (sip_dialog_t)

**定义**: Dialog 表示两个 SIP 终端之间的点对点关系，在整个会话期间保持。

**结构**:
```c
struct sip_dialog_t {
    int state;                      // 状态: EARLY/CONFIRMED/TERMINATED

    struct cstring_t callid;        // Call-ID (会话标识符)

    struct {                        // Local endpoint
        uint32_t id;                // CSeq 序列号
        uint32_t rseq;              // PRACK RSeq (RFC 3262)
        struct sip_contact_t uri;   // From/To URI
        struct sip_uri_t target;    // Contact URI
    } local, remote;                // Local and Remote

    int secure;                     // 是否使用 TLS
    struct sip_uris_t routers;      // Route Set (路由列表)

    char* ptr;                      // 内存池指针
    int32_t ref;                    // 引用计数
};
```

**状态机**:
```
DIALOG_EARLY (0)      -> 早期对话（收到 1xx 临时响应）
DIALOG_CONFIRMED (1)  -> 已确认（收到 2xx 成功响应）
DIALOG_TERMINATED (2) -> 已终止（收到 BYE）
```

**关键概念**:
- **Call-ID**: 全局唯一标识一个呼叫
- **Local/Remote tag**: From/To header 的 tag 参数，用于区分 dialog
- **CSeq**: 命令序列号，单调递增
- **Target**: 对端的 Contact URI，用于后续请求的目标地址
- **Route Set**: 记录的路由信息（从 Record-Route header 提取）

**生命周期**:
1. **创建**:
   - UAC: `sip_dialog_init_uac()` - 发送 INVITE 后创建
   - UAS: `sip_dialog_init_uas()` - 接收 INVITE 后创建
2. **维护**: 通过 `sip_dialog_addref()/release()` 管理引用计数
3. **销毁**: 引用计数归零时自动释放

**用途**:
- 在 INVITE 会话中维护会话状态
- 后续的 in-dialog 请求（BYE, UPDATE, re-INVITE等）使用 dialog 信息构造消息
- 支持 early dialog 和 confirmed dialog 两种状态

---

### 3. Message (sip_message_t)

**定义**: 表示一个完整的 SIP 消息（Request 或 Response）。

**结构**:
```c
struct sip_message_t {
    int mode;  // SIP_MESSAGE_REQUEST (0) / SIP_MESSAGE_REPLY (1)

    union {
        struct sip_requestline_t c;  // Request: METHOD URI SIP/2.0
        struct sip_statusline_t s;   // Response: SIP/2.0 CODE PHRASE
    } u;

    // RFC 3261 要求的6个核心 headers
    struct sip_contact_t to;         // To header
    struct sip_contact_t from;       // From header
    struct sip_vias_t vias;          // Via header(s)
    struct cstring_t callid;         // Call-ID header
    struct sip_cseq_t cseq;          // CSeq header
    int maxforwards;                 // Max-Forwards header

    // Contacts 和路由
    struct sip_contacts_t contacts;     // Contact header(s)
    struct sip_uris_t routers;          // Route header(s)
    struct sip_uris_t record_routers;   // Record-Route header(s)

    // 其他扩展 headers
    uint32_t rseq;                   // PRACK RSeq (RFC 3262)
    struct sip_event_t event;        // Subscribe/Notify Event
    struct sip_params_t headers;     // 其他所有 headers

    const void *payload;             // 消息体 (SDP, XML 等)
    int size;                        // 消息体大小

    struct {
        char* ptr;                   // 内存池
        char* end;
    } ptr;
};
```

**消息类型**:

**Request Methods**:
- `INVITE` - 发起会话
- `ACK` - 确认 INVITE 的最终响应
- `BYE` - 终止会话
- `CANCEL` - 取消正在处理的 INVITE
- `REGISTER` - 注册用户位置
- `OPTIONS` - 查询能力
- `PRACK` - 临时响应确认 (RFC 3262)
- `UPDATE` - 会话更新 (RFC 3311)
- `INFO` - 应用层信息 (RFC 6086)
- `MESSAGE` - 即时消息 (RFC 3428)
- `SUBSCRIBE` / `NOTIFY` - 事件订阅 (RFC 6665)
- `PUBLISH` - 事件发布 (RFC 3903)
- `REFER` - 呼叫转移 (RFC 3515)

**Response Status Codes**:
- `1xx` - 临时响应（Trying, Ringing, Session Progress）
- `2xx` - 成功响应（OK）
- `3xx` - 重定向
- `4xx` - 客户端错误（Bad Request, Unauthorized, Not Found）
- `5xx` - 服务器错误（Server Internal Error, Service Unavailable）
- `6xx` - 全局失败（Busy Everywhere, Decline）

**关键API**:
- `sip_message_create()`: 创建消息
- `sip_message_init()`: 初始化 request（提供 method, URI, from, to）
- `sip_message_init2()`: 从 dialog 初始化 request (in-dialog request)
- `sip_message_load()`: 从解析器加载消息
- `sip_message_write()`: 序列化消息到缓冲区
- `sip_message_isxxx()`: 判断消息类型的辅助函数

---

### 4. Transport (sip_transport_t)

**定义**: Transport 提供底层网络传输抽象层，支持 UDP/TCP/TLS/SCTP。

**接口**:
```c
struct sip_transport_t {
    // 生成 Via header 信息
    int (*via)(void* transport,
               const char* destination,    // 目标地址
               char protocol[16],          // OUT: UDP/TCP/TLS/SCTP
               char local[128],            // OUT: 本地地址:端口
               char dns[128]);             // OUT: Via sent-by 参数

    // 发送数据
    int (*send)(void* transport,
                const void* data,
                size_t bytes);
};
```

**工作原理**:
1. `via()` 函数：
   - 输入目标地址
   - 确定使用的传输协议（UDP/TCP/TLS）
   - 返回本地绑定地址和 DNS 名称
   - 用于构造 SIP Via header

2. `send()` 函数：
   - 发送序列化后的 SIP 消息
   - 返回 0 表示成功，<0 表示错误

**传输可靠性**:
- **UDP**: 不可靠传输，需要事务层重传（Timer A/E）
- **TCP/TLS/SCTP**: 可靠传输，不需要事务层重传

---

### 5. UAC (User Agent Client)

**定义**: UAC 是发起 SIP 请求的客户端角色。

**事务结构** (`sip_uac_transaction_t`):
```c
struct sip_uac_transaction_t {
    struct list_head link;        // 链表节点
    locker_t locker;              // 事务锁
    int32_t ref;                  // 引用计数

    struct sip_message_t* req;    // 请求消息
    uint8_t data[4*1024];         // 消息缓冲区
    int size;                     // 消息大小
    int reliable;                 // 0-UDP, 1-TCP/TLS/SCTP
    int retransmission;           // 是否启用重传

    int status;                   // 事务状态
    int retries;                  // 重传次数
    int t2;                       // 64*T1 (INVITE) / 4s (non-INVITE)
    sip_timer_t timera;           // Timer A (重传定时器)
    sip_timer_t timerb;           // Timer B (超时定时器)
    sip_timer_t timerd;           // Timer D (等待重复响应)

    struct sip_agent_t* agent;    // 所属 agent
    struct sip_dialog_t* dialog;  // 关联 dialog (INVITE)

    // 回调函数
    int (*onhandle)(struct sip_uac_transaction_t* t, int code);
    sip_uac_onsubscribe onsubscribe;
    sip_uac_oninvite oninvite;
    sip_uac_onreply onreply;
    void* param;

    struct sip_transport_t transport;  // 传输层
    void* transportptr;
};
```

**状态机** (RFC 3261 Section 17.1):

**INVITE 事务状态**:
```
CALLING       -> 发送 INVITE，启动 Timer A (重传) 和 Timer B (超时)
PROCEEDING    -> 收到 1xx 临时响应
COMPLETED     -> 收到 3xx-6xx 最终响应，发送 ACK
ACCEPTED      -> 收到 2xx 响应 (RFC 6026)
TERMINATED    -> 事务结束
```

**Non-INVITE 事务状态**:
```
TRYING        -> 发送请求，启动 Timer E (重传) 和 Timer F (超时)
PROCEEDING    -> 收到 1xx 临时响应
COMPLETED     -> 收到最终响应，启动 Timer K
TERMINATED    -> 事务结束
```

**关键API**:
- `sip_uac_invite()`: 创建 INVITE 事务
- `sip_uac_cancel()`: 创建 CANCEL 事务（取消 INVITE）
- `sip_uac_ack()`: 发送 ACK（仅用于 2xx 响应）
- `sip_uac_bye()`: 创建 BYE 事务
- `sip_uac_register()`: 创建 REGISTER 事务
- `sip_uac_send()`: 发送事务请求
- `sip_uac_transaction_addref()/release()`: 引用计数管理

**定时器**:
- **Timer A**: UDP上的 INVITE 重传定时器，初始 T1 (500ms)，指数退避到 T2 (4s)
- **Timer B**: INVITE 事务超时，64*T1 (32s)
- **Timer D**: 等待重复的响应，32s (UDP only)
- **Timer E**: Non-INVITE 重传定时器
- **Timer F**: Non-INVITE 超时定时器
- **Timer K**: Non-INVITE 完成等待

---

### 6. UAS (User Agent Server)

**定义**: UAS 是接收和响应 SIP 请求的服务器角色。

**事务结构** (`sip_uas_transaction_t`):
```c
struct sip_uas_transaction_t {
    struct list_head link;        // 链表节点
    locker_t locker;              // 事务锁
    int32_t ref;                  // 引用计数

    uint8_t data[4*1024];         // 响应消息缓冲区
    int size;                     // 消息大小
    int retransmission;           // 是否启用重传
    int reliable;                 // 0-UDP, 1-TCP/TLS/SCTP

    int32_t status;               // 事务状态
    int retries;                  // 重传次数
    int t2;                       // 64*T1 (INVITE) / 4s (non-INVITE)
    void* timerg;                 // Timer G (响应重传)
    void* timerh;                 // Timer H (超时)
    void* timerij;                // Timer I/J (等待结束)

    struct sip_agent_t* agent;    // 所属 agent
    struct sip_dialog_t* dialog;  // 关联 dialog
    struct sip_uas_handler_t* handler;  // 回调处理器
    void* initparam;              // 初始参数

    struct sip_message_t* reply;  // 响应消息（临时）
};
```

**状态机** (RFC 3261 Section 17.2):

**INVITE 事务状态**:
```
INIT          -> 收到 INVITE
TRYING        -> 准备响应
PROCEEDING    -> 发送 1xx 临时响应
COMPLETED     -> 发送 3xx-6xx 最终响应，等待 ACK
ACCEPTED      -> 发送 2xx 响应 (RFC 6026)
CONFIRMED     -> 收到 ACK (仅3xx-6xx)
TERMINATED    -> 事务结束
```

**Non-INVITE 事务状态**:
```
INIT          -> 收到请求
TRYING        -> 准备响应
PROCEEDING    -> 发送 1xx 临时响应
COMPLETED     -> 发送最终响应，启动 Timer J
TERMINATED    -> 事务结束
```

**回调接口** (`sip_uas_handler_t`):
```c
struct sip_uas_handler_t {
    // 网络发送回调
    int (*send)(void* param, ...);

    // 请求处理回调
    int (*onregister)(void* param, const struct sip_message_t* req, ...);
    int (*oninvite)(void* param, const struct sip_message_t* req, ...);
    int (*onack)(void* param, const struct sip_message_t* req, ...);
    int (*onbye)(void* param, const struct sip_message_t* req, ...);
    int (*oncancel)(void* param, const struct sip_message_t* req, ...);
    int (*onprack)(void* param, const struct sip_message_t* req, ...);
    int (*onupdate)(void* param, const struct sip_message_t* req, ...);
    int (*oninfo)(void* param, const struct sip_message_t* req, ...);
    int (*onmessage)(void* param, const struct sip_message_t* req, ...);
    int (*onsubscribe)(void* param, const struct sip_message_t* req, ...);
    int (*onnotify)(void* param, const struct sip_message_t* req, ...);
    int (*onpublish)(void* param, const struct sip_message_t* req, ...);
    int (*onrefer)(void* param, const struct sip_message_t* req, ...);
};
```

**关键API**:
- `sip_uas_reply()`: 发送响应（临时或最终）
- `sip_uas_add_header()`: 添加响应头
- `sip_uas_transaction_addref()/release()`: 引用计数管理

**定时器**:
- **Timer G**: INVITE 响应重传定时器（UDP only）
- **Timer H**: INVITE 等待 ACK 超时，64*T1 (32s)
- **Timer I**: INVITE ACK 等待定时器，T4 (5s, UDP only)
- **Timer J**: Non-INVITE 完成等待，64*T1 (32s, UDP only)

---

## UAC 发起呼叫流程

### 完整呼叫流程图

```
UAC (Caller)                  SIP Server               UAS (Callee)
    |                              |                         |
    |------ INVITE (SDP offer) --->|                         |
    |<----- 100 Trying ------------|                         |
    |                              |------ INVITE ---------->|
    |                              |<----- 180 Ringing ------|
    |<----- 180 Ringing -----------|                         |
    |                              |                         | (用户响铃)
    |                              |<----- 200 OK (SDP) -----|
    |<----- 200 OK (SDP answer) ---|                         |
    |------ ACK ------------------>|                         |
    |                              |------ ACK ------------->|
    |<======================RTP Media======================>|
    |------ BYE ------------------>|                         |
    |                              |------ BYE ------------->|
    |                              |<----- 200 OK -----------|
    |<----- 200 OK ----------------|                         |
    |                              |                         |
```

### 详细步骤

#### 第1步: 创建 INVITE 事务

```c
// 创建 SIP agent
struct sip_uas_handler_t handler = {
    .send = transport_send_callback,
    .oninvite = on_uas_invite,
    .onack = on_uas_ack,
    .onbye = on_uas_bye,
    // ... 其他回调
};

struct sip_agent_t* agent = sip_agent_create(&handler);

// 创建 INVITE 事务
struct sip_uac_transaction_t* t = sip_uac_invite(
    agent,
    "Alice <sip:alice@example.com>",  // from
    "sip:bob@example.com",            // to
    on_uac_invite_reply,              // 响应回调
    user_data                         // 用户参数
);
```

**libsip 内部流程**:
1. 创建 REQUEST 消息
2. 初始化消息 headers（Call-ID, From, To, CSeq 等）
3. 创建 UAC 事务，状态设置为 `CALLING`
4. 返回事务句柄

#### 第2步: 添加 SDP 并发送

```c
// SDP offer 示例
const char* sdp =
    "v=0\r\n"
    "o=alice 2890844526 2890844526 IN IP4 192.168.1.100\r\n"
    "s=Session SDP\r\n"
    "c=IN IP4 192.168.1.100\r\n"
    "t=0 0\r\n"
    "m=audio 49170 RTP/AVP 0\r\n"
    "a=rtpmap:0 PCMU/8000\r\n";

// 实现 transport 接口
struct sip_transport_t transport = {
    .via = my_transport_via,
    .send = my_transport_send
};

// 发送 INVITE
int ret = sip_uac_send(t, sdp, strlen(sdp), &transport, transport_param);
```

**发送的 INVITE 消息示例**:
```
INVITE sip:bob@example.com SIP/2.0
Via: SIP/2.0/TCP 192.168.1.100:5060;branch=z9hG4bKnashds8
Max-Forwards: 70
From: Alice <sip:alice@example.com>;tag=1928301774
To: <sip:bob@example.com>
Call-ID: a84b4c76e66710@pc33.atlanta.com
CSeq: 1 INVITE
Contact: <sip:alice@192.168.1.100:5060>
Content-Type: application/sdp
Content-Length: 142

v=0
o=alice 2890844526 2890844526 IN IP4 192.168.1.100
s=Session SDP
c=IN IP4 192.168.1.100
t=0 0
m=audio 49170 RTP/AVP 0
a=rtpmap:0 PCMU/8000
```

#### 第3步: 接收响应

**UAC INVITE 响应回调**:
```c
static int on_uac_invite_reply(void* param,
                                const struct sip_message_t* reply,
                                struct sip_uac_transaction_t* t,
                                struct sip_dialog_t* dialog,
                                const struct cstring_t* id,
                                int code)
{
    if (code >= 100 && code < 200) {
        // 临时响应 (100 Trying, 180 Ringing, 183 Session Progress)
        printf("Call progress: %d\n", code);

        if (code == 180) {
            printf("Callee is ringing...\n");
        }
    }
    else if (code == 200) {
        // 200 OK - 呼叫成功
        printf("Call answered!\n");

        // 保存 dialog 用于后续 in-dialog 请求
        if (dialog) {
            sip_dialog_addref(dialog);
            // 保存 dialog 到应用层数据结构
            save_dialog(dialog);
        }

        // 提取 SDP answer
        if (reply->payload && reply->size > 0) {
            process_sdp_answer(reply->payload, reply->size);
        }

        // 发送 ACK
        sip_uac_ack(t, NULL, 0, NULL);

        // 启动 RTP 媒体传输
        start_rtp_session();
    }
    else if (code >= 300) {
        // 错误响应
        printf("Call failed: %d\n", code);

        // 对于 3xx-6xx，libsip 会自动发送 ACK
        // 清理资源
        cleanup_call();
    }

    return 0;
}
```

#### 第4步: 发送 BYE 挂断

```c
// 使用已保存的 dialog 发送 BYE
struct sip_dialog_t* dialog = get_saved_dialog();

struct sip_uac_transaction_t* bye_t = sip_uac_bye(
    agent,
    dialog,
    on_uac_bye_reply,
    user_data
);

// 发送 BYE
sip_uac_send(bye_t, NULL, 0, &transport, transport_param);
```

**BYE 响应回调**:
```c
static int on_uac_bye_reply(void* param,
                            const struct sip_message_t* reply,
                            struct sip_uac_transaction_t* t,
                            int code)
{
    if (code == 200) {
        printf("BYE confirmed\n");

        // 停止 RTP
        stop_rtp_session();

        // 释放 dialog
        struct sip_dialog_t* dialog = get_saved_dialog();
        if (dialog) {
            sip_dialog_release(dialog);
        }

        // 清理资源
        cleanup_call();
    }

    return 0;
}
```

---

## UAS 接收呼叫流程

### 详细步骤

#### 第1步: 接收 INVITE

**UAS INVITE 回调实现**:
```c
static int on_uas_invite(void* param,
                         const struct sip_message_t* req,
                         struct sip_uas_transaction_t* t,
                         struct sip_dialog_t* redialog,
                         const struct cstring_t* id,
                         const void* sdp,
                         int sdp_len)
{
    printf("Incoming call from: %.*s\n",
           (int)req->from.uri.host.n,
           req->from.uri.host.p);

    // 保存 transaction 用于后续应答
    save_uas_transaction(t);
    sip_uas_transaction_addref(t);

    // 处理 SDP offer
    if (sdp && sdp_len > 0) {
        process_sdp_offer(sdp, sdp_len);
    }

    // 可以立即发送 180 Ringing
    sip_uas_reply(t, 180, NULL, 0, param);

    // 通知应用层有来电
    // 应用层决定何时应答（调用 answer_call）
    notify_incoming_call(req);

    return 0;
}
```

#### 第2步: 应答呼叫

```c
// 用户接听电话时调用
void answer_call(void)
{
    struct sip_uas_transaction_t* t = get_saved_uas_transaction();

    // 生成 SDP answer
    char sdp[1024];
    int sdp_len = generate_sdp_answer(sdp, sizeof(sdp));

    // 发送 200 OK
    sip_uas_reply(t, 200, sdp, sdp_len, user_param);

    // 启动 RTP session
    start_rtp_session();
}
```

#### 第3步: 接收 ACK

**UAS ACK 回调实现**:
```c
static int on_uas_ack(void* param,
                      const struct sip_message_t* req,
                      struct sip_uas_transaction_t* t,
                      struct sip_dialog_t* dialog,
                      const struct cstring_t* id,
                      int code,
                      const void* data,
                      int bytes)
{
    printf("ACK received, call established\n");

    // 保存 dialog 用于后续 in-dialog 请求
    if (dialog) {
        sip_dialog_addref(dialog);
        save_dialog(dialog);
    }

    // Dialog 状态: DIALOG_CONFIRMED
    // 呼叫已完全建立

    return 0;
}
```

#### 第4步: 接收 BYE

**UAS BYE 回调实现**:
```c
static int on_uas_bye(void* param,
                      const struct sip_message_t* req,
                      struct sip_uas_transaction_t* t,
                      const struct cstring_t* id)
{
    printf("Received BYE, call ending\n");

    // 停止 RTP
    stop_rtp_session();

    // 发送 200 OK
    sip_uas_reply(t, 200, NULL, 0, param);

    // 释放 dialog
    struct sip_dialog_t* dialog = get_saved_dialog();
    if (dialog) {
        sip_dialog_release(dialog);
    }

    // 清理资源
    cleanup_call();

    return 0;
}
```

---

## 关键特性

### 1. 事务管理

**事务匹配规则** (RFC 3261 Section 17.1.3):
- **Via branch parameter**: `z9hG4bK` 前缀 + 唯一标识符
- UAC transaction: 通过 branch 匹配响应
- UAS transaction: 通过 branch 匹配请求重传

**事务生命周期**:
1. **创建**: 发送请求 (UAC) 或接收请求 (UAS)
2. **活跃**: 等待响应或处理响应
3. **完成**: 收到最终响应或发送最终响应
4. **终止**: 定时器超时，清理资源

**引用计数**:
- 每个事务都有引用计数
- agent 持有引用
- 应用层可以 addref 延长生命周期
- 引用归零时自动销毁

### 2. Dialog 管理

**Dialog 标识**:
- Call-ID
- Local tag (From tag for UAC, To tag for UAS)
- Remote tag (To tag for UAC, From tag for UAS)

**Dialog 状态**:
- Early Dialog: 收到 1xx 临时响应后创建
- Confirmed Dialog: 收到 2xx 成功响应 + ACK 后确认
- Terminated Dialog: 收到 BYE 后终止

**In-Dialog 请求**:
使用 dialog 信息构造后续请求（BYE, re-INVITE, UPDATE 等）：
- Request-URI = remote target
- Route headers from route set
- Call-ID 保持不变
- From/To with tags
- CSeq 递增

### 3. 可靠性与定时器

**UDP 重传机制**:
- INVITE 请求: Timer A (T1, 2T1, 4T1, ... T2)
- INVITE 响应: Timer G (T1, 2T1, 4T1, ... T2)
- Non-INVITE: Timer E (T1, 2T1, 4T1, ... T2)

**超时保护**:
- Timer B: INVITE 事务超时 (64*T1 = 32s)
- Timer F: Non-INVITE 事务超时 (64*T1 = 32s)
- Timer H: 等待 ACK 超时 (64*T1 = 32s)

**TCP 传输**:
- 不需要重传（TCP 保证可靠性）
- 仍然需要超时定时器
- Timer B/F/H 仍然有效

### 4. SDP 协商

**Offer/Answer Model** (RFC 3264):
1. **Offer**: UAC 在 INVITE 中发送 SDP offer
   - 列出支持的媒体类型、编解码器、端口等
2. **Answer**: UAS 在 200 OK 中发送 SDP answer
   - 选择最终使用的编解码器和参数
3. **Negotiation**: 双方确定媒体参数
4. **Re-negotiation**: 通过 re-INVITE 或 UPDATE 修改媒体参数

**SDP 示例**:
```
v=0                                 # Version
o=alice 2890844526 2890844526 IN IP4 192.168.1.100  # Origin
s=Session SDP                       # Session name
c=IN IP4 192.168.1.100             # Connection info
t=0 0                              # Time
m=audio 49170 RTP/AVP 0 8 3        # Media description
a=rtpmap:0 PCMU/8000               # Codec mapping
a=rtpmap:8 PCMA/8000
a=rtpmap:3 GSM/8000
```

### 5. 错误处理

**常见错误码**:
- `400 Bad Request`: 请求语法错误
- `401 Unauthorized`: 需要认证
- `403 Forbidden`: 禁止访问
- `404 Not Found`: 用户不存在
- `408 Request Timeout`: 请求超时
- `480 Temporarily Unavailable`: 临时不可达
- `486 Busy Here`: 忙
- `487 Request Terminated`: 请求被 CANCEL
- `500 Server Internal Error`: 服务器错误
- `503 Service Unavailable`: 服务不可用

---

## 使用示例

### 完整的 UAC 实现示例

```c
#include "sip-agent.h"
#include "sip-uac.h"
#include "sip-dialog.h"

// 应用层数据结构
struct app_context {
    struct sip_agent_t* agent;
    struct sip_dialog_t* dialog;
    struct sip_uac_transaction_t* invite_t;
    // ... 其他状态
};

// UAC 回调
static int on_invite_reply(void* param,
                          const struct sip_message_t* reply,
                          struct sip_uac_transaction_t* t,
                          struct sip_dialog_t* dialog,
                          const struct cstring_t* id,
                          int code)
{
    struct app_context* ctx = (struct app_context*)param;

    if (code == 200) {
        // 保存 dialog
        ctx->dialog = dialog;
        sip_dialog_addref(dialog);

        // 发送 ACK
        sip_uac_ack(t, NULL, 0, NULL);

        printf("Call connected!\n");
    }

    return 0;
}

// 发起呼叫
void make_call(struct app_context* ctx, const char* target)
{
    // 创建 INVITE 事务
    ctx->invite_t = sip_uac_invite(
        ctx->agent,
        "sip:alice@example.com",
        target,
        on_invite_reply,
        ctx
    );

    // 准备 SDP
    const char* sdp = "v=0\r\n...";

    // 发送 INVITE
    sip_uac_send(ctx->invite_t, sdp, strlen(sdp), &transport, NULL);
}

// 挂断
void hangup(struct app_context* ctx)
{
    if (ctx->dialog) {
        struct sip_uac_transaction_t* bye_t = sip_uac_bye(
            ctx->agent,
            ctx->dialog,
            NULL,
            ctx
        );

        sip_uac_send(bye_t, NULL, 0, &transport, NULL);

        sip_dialog_release(ctx->dialog);
        ctx->dialog = NULL;
    }
}
```

---

## 总结

### libsip 架构优势

1. **清晰的分层**:
   - Agent 层: 管理和分发
   - Transaction 层: 可靠性和状态机
   - Dialog 层: 会话管理
   - Transport 层: 网络抽象

2. **RFC 3261 完整实现**:
   - 支持所有基本 SIP 方法
   - 完整的事务状态机
   - 正确的定时器和重传机制
   - Dialog 管理

3. **灵活的回调机制**:
   - 应用层可以完全控制 SIP 操作
   - 支持同步和异步处理
   - 事件驱动架构

4. **引用计数管理**:
   - 避免内存泄漏
   - 支持异步操作
   - 正确的资源释放

### 典型应用场景

1. **VoIP 软电话**: 实现 SIP UAC/UAS 功能
2. **SIP 代理服务器**: 作为中间层转发 SIP 消息
3. **会议服务器**: 管理多方会话
4. **IVR 系统**: 自动语音应答系统
5. **媒体网关**: SIP 与其他协议互通

---

## 参考资料

- RFC 3261: SIP: Session Initiation Protocol
- RFC 3262: Reliability of Provisional Responses in SIP (PRACK)
- RFC 3264: An Offer/Answer Model with SDP
- RFC 3311: SIP UPDATE Method
- RFC 3428: SIP Extension for Instant Messaging (MESSAGE)
- RFC 3515: SIP Refer Method (REFER)
- RFC 6026: Correct Transaction Handling for 2xx Responses to INVITE
- RFC 6665: SIP-Specific Event Notification (SUBSCRIBE/NOTIFY)

---

**文档版本**: 1.0
**创建日期**: 2025-01-06
**项目**: libsip (media-server)
