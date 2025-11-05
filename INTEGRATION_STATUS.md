# lwsip v2.0 - libsip Integration Status

## 已完成的集成 (✅)

### 1. lws_client.c - SIP Agent 集成

**完成的功能**:
- ✅ 包含libsip头文件 (sip-agent.h, sip-message.h, sip-uas.h)
- ✅ 在 `lws_client_start()` 中创建 sip_agent
- ✅ 在 `lws_client_stop()` 中销毁 sip_agent
- ✅ 实现 SIP transport send 回调 (`sip_transport_send`)
- ✅ 实现 HTTP parser 解析 SIP 消息 (`on_transport_recv`)
- ✅ 将解析的消息输入到 sip_agent (`sip_agent_input`)

**实现的 UAS Handler 回调**:
- ✅ `sip_uas_onregister` - 处理 REGISTER 响应
- ✅ `sip_uas_oninvite` - 处理来电 INVITE
- ✅ `sip_uas_onack` - 处理 ACK
- ✅ `sip_uas_onbye` - 处理 BYE (挂断)
- ✅ `sip_uas_oncancel` - 处理 CANCEL
- ✅ 其他方法的stub处理器 (onprack, onupdate, oninfo, 等)

**关键代码位置**:
- 文件: `v2.0/src/lws_client.c`
- SIP agent创建: lws_client.c:464
- UAS handler定义: lws_client.c:338-353
- 消息解析: lws_client.c:79-138

### 2. lws_uac.c - UAC 功能实现

**完成的功能**:
- ✅ 包含libsip UAC头文件 (sip-uac.h)
- ✅ 实现 `lws_uac_register()` - 发送 REGISTER
- ✅ 实现 `lws_uac_invite()` - 发送 INVITE with SDP
- ✅ 实现 UAC 回调处理 (on_register_reply, on_invite_reply, on_bye_reply)

**REGISTER 实现**:
```c
// 构建From URI: sip:username@server
// 构建registrar URI: sip:server:port
// 调用 sip_uac_register() 创建事务
// 调用 sip_uac_send() 发送请求
```

**INVITE 实现**:
```c
// 生成 SDP offer (通过 lws_session)
// 构建 From URI
// 调用 sip_uac_invite() 创建事务
// 添加 Content-Type: application/sdp 头
// 调用 sip_uac_send() 发送 INVITE
```

**关键代码位置**:
- 文件: `v2.0/src/lws_uac.c`
- REGISTER实现: lws_uac.c:149-220
- INVITE实现: lws_uac.c:251-332
- 回调处理: lws_uac.c:46-137

### 3. lws_error.h - 错误码扩展

**新增错误码**:
- ✅ `LWS_ERR_SIP_CREATE` (0x80020006)
- ✅ `LWS_ERR_SIP_INPUT` (0x80020007)
- ✅ `LWS_ERR_SIP_SEND` (0x80020008)
- ✅ `LWS_ERR_SDP_GENERATE` (0x80050005)
- ✅ `LWS_ERR_SDP_PARSE` (0x80050006)

### 4. lws_types.h - 状态扩展

**新增呼叫状态**:
- ✅ `LWS_CALL_ESTABLISHED` - 呼叫已建立
- ✅ `LWS_CALL_FAILED` - 呼叫失败
- ✅ `LWS_CALL_TERMINATED` - 呼叫已终止

## 待完善的部分 (🔄)

### 1. Dialog 管理

**问题**: 当前实现没有维护 SIP dialog 状态

**影响**:
- BYE 需要 dialog 信息才能发送到正确的对端
- Re-INVITE 需要 dialog 状态

**建议解决方案**:
```c
// 在 lws_client 中添加 dialog 管理
typedef struct {
    struct sip_dialog_t* dialog;
    lws_session_t* session;
    char peer_uri[256];
} lws_call_t;

// 维护活动呼叫列表
lws_call_t* active_calls[MAX_CALLS];
```

### 2. BYE 实现

**当前状态**: 仅为stub，未实际发送 BYE

**需要实现**:
```c
// 需要从 dialog 中获取信息
int lws_uac_bye(lws_client_t* client, struct sip_dialog_t* dialog) {
    struct sip_uac_transaction_t* t;
    t = sip_uac_bye(client->sip_agent, dialog, on_bye_reply, client);
    sip_uac_send(t, NULL, 0, &s_uac_transport, client);
}
```

### 3. 认证 (Authentication)

**当前状态**: 未实现 401/407 认证响应处理

**需要实现**:
- 在 on_register_reply 中处理 401 Unauthorized
- 使用 username/password 生成 Authorization 头
- 重新发送带认证的请求

### 4. libsip Transport Integration

**问题**: `s_uac_transport.send` 当前为 NULL

**需要修复**:
```c
// 在 lws_client.c 中设置
static struct sip_transport_t s_uac_transport = {
    .via = NULL,
    .send = sip_transport_send,  // 使用 lws_client 中的实现
};
```

### 5. HTTP Parser 模式选择

**问题**: on_transport_recv 始终使用 HTTP_PARSER_REQUEST

**需要改进**:
```c
// 根据消息类型选择模式
// SIP响应: HTTP_PARSER_RESPONSE
// SIP请求: HTTP_PARSER_REQUEST
enum HTTP_PARSER_MODE mode = (data[0] == 'S') ?
    HTTP_PARSER_RESPONSE : HTTP_PARSER_REQUEST;
```

### 6. SDP 生成和解析

**依赖**: 需要先实现 lws_session.c 中的 SDP 功能

**接口**:
```c
int lws_session_generate_sdp_offer(lws_session_t* session,
    const char* local_ip, char* sdp, int size);
int lws_session_process_sdp(lws_session_t* session,
    const char* sdp, int len);
```

## 测试计划

### 单元测试
1. ✅ 编译通过
2. 🔄 创建 client 对象
3. 🔄 启动 SIP agent
4. 🔄 发送 REGISTER
5. 🔄 接收 REGISTER 响应

### 集成测试 (with FreeSWITCH)
1. 🔄 REGISTER 注册成功
2. 🔄 发起 INVITE 呼叫
3. 🔄 接收 INVITE (被叫)
4. 🔄 SDP 协商
5. 🔄 RTP 媒体传输
6. 🔄 BYE 挂断

## 下一步工作优先级

1. **高优先级** (必须):
   - 实现 Dialog 管理
   - 修复 HTTP parser 模式选择
   - 实现 BYE 功能
   - 实现认证处理

2. **中优先级** (重要):
   - 完善 lws_session.c (SDP 生成/解析)
   - 实现 RTP socket 收发
   - 错误处理和重试机制

3. **低优先级** (可选):
   - CANCEL 实现
   - Re-INVITE 实现
   - OPTIONS 实现

## 文件修改总结

### 修改的文件:
1. `v2.0/src/lws_client.c` - 346行 → 503行 (+157行)
2. `v2.0/src/lws_uac.c` - 139行 → 365行 (+226行)
3. `v2.0/include/lws_error.h` - 增加5个错误码
4. `v2.0/include/lws_types.h` - 增加3个呼叫状态

### 代码统计:
- 总新增代码: ~380行
- 集成libsip接口: 8个主要函数
- 实现回调: 16个回调函数

---

**更新时间**: 2025-01
**状态**: libsip基础集成完成，待测试和完善
