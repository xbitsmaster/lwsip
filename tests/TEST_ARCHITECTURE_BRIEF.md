# lwsip 测试架构简报
**版本**: v1.0
**日期**: 2025年11月
**目的**: 为lwsip项目设计完整的测试架构，验证lws_agent的UAC/UAS功能

---

## 一、测试目标

验证lwsip的SIP Agent层（lws_agent）完整功能，包括：

1. **UAC（主叫方）功能**
   - SIP注册流程
   - 发起呼叫（INVITE）
   - SDP Offer/Answer协商
   - 呼叫状态管理
   - 挂断流程（BYE）

2. **UAS（被叫方）功能**
   - SIP注册流程
   - 接收来电（INVITE）
   - 自动应答（200 OK）
   - SDP Offer/Answer协商
   - 接收挂断（BYE）

3. **媒体会话协调**
   - lws_agent与lws_sess的集成
   - SDP生成和解析
   - 媒体会话生命周期管理

---

## 二、测试架构设计

### 2.1 架构图

```
┌──────────────┐         ┌──────────────┐         ┌──────────────┐
│   caller.c   │         │ sip_fake.c   │         │   callee.c   │
│   (UAC)      │◄───────►│ (SIP Server) │◄───────►│   (UAS)      │
│              │  SIP    │              │  SIP    │              │
│ lws_agent    │         │ Simple       │         │ lws_agent    │
│   ↓          │         │ Routing      │         │   ↓          │
│ sess_stub    │         │              │         │ sess_stub    │
└──────────────┘         └──────────────┘         └──────────────┘
```

### 2.2 测试流程

```
caller.c (1001)                  sip_fake.c                  callee.c (1000)
     │                                │                           │
     ├─── REGISTER (1001) ──────────►│                           │
     │◄────── 200 OK ─────────────────┤                           │
     │                                │◄─── REGISTER (1000) ──────┤
     │                                ├────── 200 OK ────────────►│
     │                                │                           │
     ├─── INVITE (1001→1000) ───────►│                           │
     │    + SDP Offer                 ├─── INVITE ──────────────►│
     │                                │                           ├─ on_incoming_call()
     │                                │                           ├─ lws_agent_answer()
     │                                │◄─── 200 OK ───────────────┤
     │◄────── 200 OK ─────────────────┤    + SDP Answer           │
     │    + SDP Answer                │                           │
     ├─ on_remote_sdp()               │                           ├─ on_remote_sdp()
     │                                │                           │
     ├─── ACK ──────────────────────►│                           │
     │                                ├─── ACK ─────────────────►│
     │                                │                           │
     │         【媒体会话 - 使用sess_stub】                      │
     │                                │                           │
     ├─── BYE ──────────────────────►│                           │
     │                                ├─── BYE ─────────────────►│
     │                                │◄─── 200 OK ───────────────┤
     │◄────── 200 OK ─────────────────┤                           │
     │                                │                           │
```

---

## 三、各模块设计详细说明

### 3.1 sip_fake.c - 简化SIP服务器

**职责**：
- 接收并处理REGISTER请求（简单注册表管理）
- 路由INVITE消息（1001→1000）
- 转发所有SIP应答消息
- 处理ACK和BYE消息

**不依赖libsip**：
- 使用简单的字符串解析
- 维护最小注册表（map: username → address）
- 无状态或最小状态机

**核心功能**：
```c
// 简单的SIP消息解析
typedef struct {
    char method[32];           // REGISTER, INVITE, ACK, BYE, etc.
    char uri[256];             // sip:1000@example.com
    char from[256];            // From header
    char to[256];              // To header
    char call_id[128];         // Call-ID
    char* body;                // SDP body
    int content_length;
} sip_fake_msg_t;

// 注册表
typedef struct {
    char username[64];         // e.g., "1000"
    struct sockaddr_in addr;   // IP:Port
    time_t expires;
} registration_entry_t;

// 主函数
int main() {
    // 1. 创建UDP socket (监听5060)
    // 2. 主循环:
    //    - 接收SIP消息
    //    - 解析消息类型
    //    - 根据类型处理:
    //      - REGISTER: 更新注册表，返回200 OK
    //      - INVITE: 查找被叫方，转发INVITE
    //      - 200 OK/18x: 根据Call-ID转发给主叫方
    //      - ACK/BYE: 转发到对方
}
```

**实现简化**：
- 不处理身份验证（接受所有REGISTER）
- 不处理SIP事务超时
- 不支持多个并发呼叫（单个Call-ID路由即可）
- 固定路由：1001 ↔ 1000

---

### 3.2 caller.c - 主叫方（UAC）

**基于 arch-v3.0.md lines 689-801 的完整UAC流程**

**核心流程**：
```c
int main() {
    // ========================================
    // 步骤 1: 初始化传输层
    // ========================================
    lws_trans_config_t trans_cfg;
    lws_trans_init_udp_config(&trans_cfg, 0);  // 自动端口

    lws_trans_handler_t trans_handler = {
        .on_recv = on_trans_recv,
        .userdata = &app_ctx
    };
    lws_trans_t* trans = lws_trans_create(&trans_cfg, &trans_handler);
    lws_trans_open(trans);

    // ========================================
    // 步骤 2: 创建 SIP Agent (1001)
    // ========================================
    lws_agent_config_t agent_cfg;
    lws_agent_init_default_config(&agent_cfg, "1001", "1234",
                                   "localhost", trans);

    lws_agent_handler_t agent_handler = {
        .on_register_result = on_register_result,
        .on_dialog_state_changed = on_dialog_state_changed,
        .on_remote_sdp = on_remote_sdp,
        .userdata = &app_ctx
    };
    lws_agent_t* agent = lws_agent_create(&agent_cfg, &agent_handler);
    lws_agent_start(agent);  // 开始注册到localhost:5060

    // ========================================
    // 步骤 3: 创建媒体会话（使用sess_stub）
    // ========================================
    lws_sess_config_t sess_cfg;
    lws_sess_init_audio_config(&sess_cfg, "stun.example.com",
                                LWS_AUDIO_CODEC_PCMU);

    lws_sess_handler_t sess_handler = {
        .on_sdp_ready = on_sdp_ready,
        .on_connected = on_media_connected,
        .userdata = &app_ctx
    };
    lws_sess_t* session = lws_sess_create(&sess_cfg, &sess_handler);

    // ========================================
    // 步骤 4: 收集 ICE candidates
    // ========================================
    lws_sess_gather_candidates(session);

    // 回调函数在 on_sdp_ready 中发起呼叫

    // ========================================
    // 步骤 5: 主循环
    // ========================================
    while (running && !call_completed) {
        lws_agent_loop(agent, 10);   // SIP 信令处理
        lws_sess_loop(session, 10);  // 媒体会话处理
        lws_trans_loop(trans, 10);   // 网络接收

        // 模拟通话15秒后挂断
        if (call_established && (time(NULL) - call_start_time > 15)) {
            lws_agent_hangup(agent, dialog);
            call_completed = 1;
        }
    }

    // ========================================
    // 步骤 6: 清理
    // ========================================
    lws_sess_destroy(session);
    lws_agent_destroy(agent);
    lws_trans_destroy(trans);

    return 0;
}

// 回调函数实现
void on_sdp_ready(lws_sess_t* sess, const char* sdp, void* userdata) {
    printf("[CALLER] 本地 SDP 就绪，发起呼叫到 1000...\n");

    // 发起呼叫
    lws_dialog_t* dialog = lws_agent_make_call(agent,
                                                "sip:1000@localhost",
                                                sdp);
    app_ctx->dialog = dialog;
}

void on_remote_sdp(lws_agent_t* agent, lws_dialog_t* dialog,
                   const char* sdp, void* userdata) {
    printf("[CALLER] 收到远端 SDP，设置到媒体会话...\n");

    // 设置远端 SDP
    lws_sess_set_remote_sdp(session, sdp);

    // 开始 ICE 连接
    lws_sess_start_ice(session);
}

void on_media_connected(lws_sess_t* sess, void* userdata) {
    printf("[CALLER] 媒体已连接！\n");
    call_established = 1;
    call_start_time = time(NULL);
}
```

**关键点**：
- 使用lws_agent API（不直接操作libsip）
- 在`on_sdp_ready`回调中发起呼叫
- 在`on_remote_sdp`回调中启动ICE
- 模拟通话15秒后主动挂断

---

### 3.3 callee.c - 被叫方（UAS）

**基于 lws_agent.h 回调接口设计的UAS流程**

**核心流程**：
```c
int main() {
    // ========================================
    // 步骤 1: 初始化传输层
    // ========================================
    lws_trans_config_t trans_cfg;
    lws_trans_init_udp_config(&trans_cfg, 0);  // 自动端口

    lws_trans_handler_t trans_handler = {
        .on_recv = on_trans_recv,
        .userdata = &app_ctx
    };
    lws_trans_t* trans = lws_trans_create(&trans_cfg, &trans_handler);
    lws_trans_open(trans);

    // ========================================
    // 步骤 2: 创建 SIP Agent (1000)
    // ========================================
    lws_agent_config_t agent_cfg;
    lws_agent_init_default_config(&agent_cfg, "1000", "1234",
                                   "localhost", trans);

    lws_agent_handler_t agent_handler = {
        .on_register_result = on_register_result,
        .on_incoming_call = on_incoming_call,    // UAS关键回调
        .on_dialog_state_changed = on_dialog_state_changed,
        .on_remote_sdp = on_remote_sdp,
        .userdata = &app_ctx
    };
    lws_agent_t* agent = lws_agent_create(&agent_cfg, &agent_handler);
    lws_agent_start(agent);  // 开始注册到localhost:5060

    // ========================================
    // 步骤 3: 创建媒体会话（使用sess_stub）
    // ========================================
    lws_sess_config_t sess_cfg;
    lws_sess_init_audio_config(&sess_cfg, "stun.example.com",
                                LWS_AUDIO_CODEC_PCMU);

    lws_sess_handler_t sess_handler = {
        .on_sdp_ready = on_sdp_ready,
        .on_connected = on_media_connected,
        .userdata = &app_ctx
    };
    lws_sess_t* session = lws_sess_create(&sess_cfg, &sess_handler);

    // 预先收集 ICE candidates
    lws_sess_gather_candidates(session);

    // ========================================
    // 步骤 4: 主循环（等待来电）
    // ========================================
    printf("[CALLEE] 等待来电...\n");

    while (running) {
        lws_agent_loop(agent, 10);   // SIP 信令处理
        lws_sess_loop(session, 10);  // 媒体会话处理
        lws_trans_loop(trans, 10);   // 网络接收
    }

    // ========================================
    // 步骤 5: 清理
    // ========================================
    lws_sess_destroy(session);
    lws_agent_destroy(agent);
    lws_trans_destroy(trans);

    return 0;
}

// UAS关键回调实现
void on_incoming_call(lws_agent_t* agent, lws_dialog_t* dialog,
                      const lws_sip_addr_t* from, void* userdata) {
    printf("[CALLEE] 收到来自 %s@%s 的来电\n", from->username, from->domain);

    // 自动应答
    printf("[CALLEE] 自动应答来电...\n");

    // 应答呼叫（传递本地SDP）
    lws_agent_answer(agent, dialog, app_ctx->local_sdp);

    app_ctx->dialog = dialog;
}

void on_sdp_ready(lws_sess_t* sess, const char* sdp, void* userdata) {
    printf("[CALLEE] 本地 SDP 就绪\n");

    // 保存本地SDP，用于应答来电
    app_ctx->local_sdp = strdup(sdp);
}

void on_remote_sdp(lws_agent_t* agent, lws_dialog_t* dialog,
                   const char* sdp, void* userdata) {
    printf("[CALLEE] 收到远端 SDP\n");

    // 设置远端 SDP
    lws_sess_set_remote_sdp(session, sdp);

    // 开始 ICE 连接
    lws_sess_start_ice(session);
}

void on_media_connected(lws_sess_t* sess, void* userdata) {
    printf("[CALLEE] 媒体已连接！\n");
}
```

**关键点**：
- UAS模式：监听`on_incoming_call`回调
- 收到来电后自动调用`lws_agent_answer()`应答
- 提前收集ICE candidates，SDP已就绪
- 被动等待BYE（不主动挂断）

---

### 3.4 sess_stub.c - 媒体会话存根

**职责**：
- 实现`lws_sess_*`函数的存根版本
- 不进行真实的ICE/RTP操作
- 仅打印日志，模拟成功返回

**存根函数列表**：
```c
// ========================================
// lws_sess 存根实现
// ========================================

/**
 * @brief 初始化音频配置
 */
void lws_sess_init_audio_config(lws_sess_config_t* cfg,
                                  const char* stun_server,
                                  lws_audio_codec_t codec) {
    printf("[SESS_STUB] lws_sess_init_audio_config: stun=%s, codec=%d\n",
           stun_server, codec);
    memset(cfg, 0, sizeof(*cfg));
}

/**
 * @brief 创建会话
 */
lws_sess_t* lws_sess_create(const lws_sess_config_t* cfg,
                             const lws_sess_handler_t* handler) {
    printf("[SESS_STUB] lws_sess_create\n");

    // 分配假的会话对象
    lws_sess_t* sess = (lws_sess_t*)calloc(1, sizeof(lws_sess_t));

    // 保存handler（需要调用on_sdp_ready回调）
    sess->handler = *handler;

    return sess;
}

/**
 * @brief 收集ICE candidates
 * 模拟异步收集，立即触发on_sdp_ready回调
 */
int lws_sess_gather_candidates(lws_sess_t* sess) {
    printf("[SESS_STUB] lws_sess_gather_candidates\n");

    // 生成假的SDP
    const char* fake_sdp =
        "v=0\r\n"
        "o=- 12345 67890 IN IP4 127.0.0.1\r\n"
        "s=lwsip stub session\r\n"
        "c=IN IP4 127.0.0.1\r\n"
        "t=0 0\r\n"
        "m=audio 9000 RTP/AVP 0\r\n"
        "a=rtpmap:0 PCMU/8000\r\n";

    // 立即触发on_sdp_ready回调
    if (sess->handler.on_sdp_ready) {
        sess->handler.on_sdp_ready(sess, fake_sdp, sess->handler.userdata);
    }

    return 0;
}

/**
 * @brief 设置远端SDP
 */
int lws_sess_set_remote_sdp(lws_sess_t* sess, const char* sdp) {
    printf("[SESS_STUB] lws_sess_set_remote_sdp:\n%s\n", sdp);
    return 0;
}

/**
 * @brief 开始ICE连接
 * 模拟立即连接成功，触发on_connected回调
 */
int lws_sess_start_ice(lws_sess_t* sess) {
    printf("[SESS_STUB] lws_sess_start_ice\n");

    // 模拟立即连接成功
    if (sess->handler.on_connected) {
        sess->handler.on_connected(sess, sess->handler.userdata);
    }

    return 0;
}

/**
 * @brief 会话循环处理（空操作）
 */
int lws_sess_loop(lws_sess_t* sess, int timeout_ms) {
    // 空操作，不打印（避免日志泛滥）
    (void)sess;
    (void)timeout_ms;
    return 0;
}

/**
 * @brief 销毁会话
 */
void lws_sess_destroy(lws_sess_t* sess) {
    printf("[SESS_STUB] lws_sess_destroy\n");
    if (sess) {
        free(sess);
    }
}
```

**关键点**：
- 立即触发`on_sdp_ready`和`on_connected`回调
- 生成假的SDP（固定内容）
- 不进行网络通信
- 帮助测试SIP层逻辑而不受媒体层影响

---

## 四、编译与测试

### 4.1 Makefile集成

在`tests/Makefile`中添加新的测试目标：

```makefile
# Test components
SIP_FAKE_SRC = sip_fake.c
CALLER_SRC = caller.c
CALLEE_SRC = callee.c
SESS_STUB_SRC = sess_stub.c

# Object files
SIP_FAKE_OBJS = sip_fake.o
CALLER_OBJS = caller.o sess_stub.o lws_agent.o lws_timer.o lws_trans.o lws_trans_udp.o lws_mem.o
CALLEE_OBJS = callee.o sess_stub.o lws_agent.o lws_timer.o lws_trans.o lws_trans_udp.o lws_mem.o

# Targets
TEST_SIP_FAKE = sip_fake
TEST_CALLER = caller
TEST_CALLEE = callee

all: $(TEST_SIP_FAKE) $(TEST_CALLER) $(TEST_CALLEE)

# Build sip_fake (simple, no libsip dependency)
$(TEST_SIP_FAKE): $(SIP_FAKE_OBJS)
	$(CC) $(SIP_FAKE_OBJS) $(LDFLAGS) -o $(TEST_SIP_FAKE)

# Build caller (with libsip)
$(TEST_CALLER): $(CALLER_OBJS) $(LIBSIP_LIB) $(SDK_LIB) $(HTTP_LIB)
	$(CC) $(CALLER_OBJS) $(LIBSIP_LIB) $(HTTP_LIB) $(SDK_LIB) $(LDFLAGS) -o $(TEST_CALLER)

# Build callee (with libsip)
$(TEST_CALLEE): $(CALLEE_OBJS) $(LIBSIP_LIB) $(SDK_LIB) $(HTTP_LIB)
	$(CC) $(CALLEE_OBJS) $(LIBSIP_LIB) $(HTTP_LIB) $(SDK_LIB) $(LDFLAGS) -o $(TEST_CALLEE)

# Test target
test_call: $(TEST_SIP_FAKE) $(TEST_CALLER) $(TEST_CALLEE)
	@echo "========================================="
	@echo "运行完整呼叫测试"
	@echo "========================================="
	@./run_test.sh
```

### 4.2 测试脚本（run_test.sh）

```bash
#!/bin/bash

echo "========================================="
echo "lwsip 完整呼叫测试"
echo "========================================="
echo ""

# 启动 sip_fake 服务器
echo "[1/4] 启动 SIP 服务器 (sip_fake)..."
./sip_fake > /tmp/sip_fake.log 2>&1 &
SIP_FAKE_PID=$!
sleep 1

# 启动被叫方 (callee - 1000)
echo "[2/4] 启动被叫方 (callee - 1000)..."
./callee > /tmp/callee.log 2>&1 &
CALLEE_PID=$!
sleep 2

# 检查被叫方是否注册成功
if grep -q "registered" /tmp/callee.log; then
    echo "      ✓ 被叫方注册成功"
else
    echo "      ✗ 被叫方注册失败"
    cat /tmp/callee.log
    kill $SIP_FAKE_PID $CALLEE_PID 2>/dev/null
    exit 1
fi

# 启动主叫方 (caller - 1001)
echo "[3/4] 启动主叫方 (caller - 1001)..."
./caller > /tmp/caller.log 2>&1
CALLER_EXIT=$?

# 等待一下
sleep 1

# 清理进程
echo "[4/4] 清理测试进程..."
kill $SIP_FAKE_PID $CALLEE_PID 2>/dev/null

# 分析结果
echo ""
echo "========================================="
echo "测试结果"
echo "========================================="

if [ $CALLER_EXIT -eq 0 ]; then
    echo "✓ 主叫方执行成功"
else
    echo "✗ 主叫方执行失败 (退出码: $CALLER_EXIT)"
fi

# 检查关键日志
echo ""
echo "关键日志摘要:"
echo "----------------------------------------"
echo "【主叫方】"
grep -E "SDP 就绪|发起呼叫|收到远端 SDP|媒体已连接|挂断" /tmp/caller.log | head -10

echo ""
echo "【被叫方】"
grep -E "收到来电|应答来电|收到远端 SDP|媒体已连接" /tmp/callee.log | head -10

echo ""
echo "完整日志文件:"
echo "  SIP服务器: /tmp/sip_fake.log"
echo "  主叫方: /tmp/caller.log"
echo "  被叫方: /tmp/callee.log"
echo ""
```

---

## 五、测试验收标准

### 5.1 功能验收

1. **SIP注册**
   - [x] caller (1001) 成功注册到sip_fake
   - [x] callee (1000) 成功注册到sip_fake

2. **呼叫建立**
   - [x] caller发起INVITE到1000
   - [x] callee收到`on_incoming_call`回调
   - [x] callee自动应答（200 OK）
   - [x] caller收到200 OK
   - [x] caller发送ACK

3. **SDP协商**
   - [x] caller生成SDP Offer
   - [x] callee生成SDP Answer
   - [x] 双方收到`on_remote_sdp`回调

4. **媒体会话**
   - [x] 双方收到`on_connected`回调（sess_stub模拟）

5. **呼叫终止**
   - [x] caller在15秒后发起BYE
   - [x] callee收到BYE并应答200 OK
   - [x] dialog状态变为TERMINATED

### 5.2 日志验收

**期望在日志中看到的关键信息**：

**caller.log**:
```
[CALLER] 注册成功
[CALLER] 本地 SDP 就绪，发起呼叫到 1000...
[CALLER] 收到远端 SDP，设置到媒体会话...
[CALLER] 媒体已连接！
[CALLER] 挂断呼叫
```

**callee.log**:
```
[CALLEE] 注册成功
[CALLEE] 等待来电...
[CALLEE] 收到来自 1001@localhost 的来电
[CALLEE] 自动应答来电...
[CALLEE] 收到远端 SDP
[CALLEE] 媒体已连接！
[CALLEE] 收到挂断请求
```

**sip_fake.log**:
```
[SIP_FAKE] Received REGISTER from 1001
[SIP_FAKE] Registered: 1001 -> 127.0.0.1:xxxxx
[SIP_FAKE] Received REGISTER from 1000
[SIP_FAKE] Registered: 1000 -> 127.0.0.1:xxxxx
[SIP_FAKE] Received INVITE from 1001 to 1000
[SIP_FAKE] Forwarding INVITE to 1000
[SIP_FAKE] Received 200 OK from 1000
[SIP_FAKE] Forwarding 200 OK to 1001
[SIP_FAKE] Received ACK from 1001
[SIP_FAKE] Forwarding ACK to 1000
[SIP_FAKE] Received BYE from 1001
[SIP_FAKE] Forwarding BYE to 1000
```

---

## 六、实施计划

### 6.1 开发顺序

1. **阶段一：sess_stub.c**（最简单）
   - 实现存根函数
   - 编译测试
   - 预计工作量：2小时

2. **阶段二：sip_fake.c**（核心）
   - 实现简单SIP解析
   - 实现注册表管理
   - 实现消息路由
   - 编译测试
   - 预计工作量：6小时

3. **阶段三：caller.c**（UAC）
   - 按arch-v3.0.md实现完整UAC流程
   - 集成sess_stub
   - 编译测试
   - 预计工作量：4小时

4. **阶段四：callee.c**（UAS）
   - 实现UAS流程
   - 集成sess_stub
   - 编译测试
   - 预计工作量：4小时

5. **阶段五：集成测试**
   - 编写run_test.sh脚本
   - 端到端测试
   - 调试修复问题
   - 预计工作量：4小时

### 6.2 总工作量估算

**总计：约20小时**

---

## 七、风险与限制

### 7.1 已知限制

1. **sip_fake不是RFC 3261完整实现**
   - 仅支持基本的REGISTER/INVITE/ACK/BYE
   - 不处理认证（401/407）
   - 不支持多个并发呼叫

2. **sess_stub完全模拟**
   - 无真实ICE/STUN/RTP
   - 无法测试媒体层Bug

3. **单机测试**
   - 所有组件运行在localhost
   - 无法测试NAT穿透

### 7.2 后续改进方向

1. **增强sip_fake**
   - 添加身份验证支持
   - 支持多个并发呼叫
   - 添加简单的SIP代理功能

2. **集成真实媒体层**
   - 替换sess_stub为真实lws_sess实现
   - 测试ICE连接
   - 测试RTP发送/接收

3. **网络测试**
   - 部署到不同机器
   - 测试NAT穿透
   - 测试网络丢包/延迟

---

## 八、总结

本简报设计了一个**轻量级但完整**的测试架构：

✅ **sip_fake**: 简化SIP服务器，无libsip依赖
✅ **caller.c**: 完整UAC流程，基于arch-v3.0.md
✅ **callee.c**: 完整UAS流程，基于lws_agent回调
✅ **sess_stub.c**: 媒体层存根，隔离测试SIP层

该架构可以：
- 验证lws_agent的UAC/UAS功能
- 验证SIP消息流
- 验证回调机制
- 为后续集成真实媒体层打下基础

---

**请审阅本简报，确认以下事项：**

1. 测试架构设计是否合理？
2. 测试流程是否完整？
3. sip_fake的简化程度是否可接受？
4. sess_stub的存根方式是否满足要求？
5. 是否需要调整任何细节？

**审批通过后，我将按照阶段一至阶段五的顺序开始实施。**
