# 媒体会话层集成设计

## 1. 核心问题

当前`lws_agent.c`要求调用者提供SDP，但集成媒体会话层后，SDP由媒体会话层异步生成（需要ICE candidate gathering）。

## 2. 设计方案

### 2.1 异步SDP生成流程

由于SDP生成是异步的，需要修改接口以支持异步流程：

**UAC (发起方):**
```
lws_agent_make_call(target_uri)  // 移除local_sdp参数
  └> 创建 dialog
  └> 创建 media session (lws_sess_create)
  └> 开始收集candidates (lws_sess_gather_candidates)
  └> 返回 dialog (CALLING状态)
  └> [异步] 等待 on_sdp_ready 回调

on_sdp_ready回调:
  └> 获取 SDP (lws_sess_get_local_sdp)
  └> 发送 INVITE with SDP

收到 200 OK:
  └> 提取 remote SDP
  └> 设置 remote SDP (lws_sess_set_remote_sdp)
  └> 启动 ICE (lws_sess_start_ice)
```

**UAS (接收方):**
```
收到 INVITE (sip_uas_oninvite):
  └> 创建 dialog
  └> 创建 media session (lws_sess_create)
  └> 提取 remote SDP from INVITE
  └> 设置 remote SDP (lws_sess_set_remote_sdp)
  └> 触发 on_incoming_call 回调

lws_agent_answer_call()  // 移除local_sdp参数
  └> 开始收集candidates (lws_sess_gather_candidates)
  └> [异步] 等待 on_sdp_ready 回调

on_sdp_ready回调:
  └> 获取 SDP (lws_sess_get_local_sdp)
  └> 发送 200 OK with SDP
  └> 启动 ICE (lws_sess_start_ice)
```

### 2.2 数据结构变更

在 `lws_dialog_intl_t` 中已有的字段：
- `lws_sess_t* sess` - 媒体会话实例

无需新增字段。媒体会话状态由`lws_sess_t`内部维护。

### 2.3 接口变更

**lws_agent.h:**
```c
// 修改前:
lws_dialog_t* lws_agent_make_call(lws_agent_t* agent, const char* target_uri, const char* local_sdp);
int lws_agent_answer_call(lws_agent_t* agent, lws_dialog_t* dialog, const char* local_sdp);

// 修改后:
lws_dialog_t* lws_agent_make_call(lws_agent_t* agent, const char* target_uri);
int lws_agent_answer_call(lws_agent_t* agent, lws_dialog_t* dialog);
```

### 2.4 媒体会话回调

需要实现以下回调函数（在lws_agent.c中）:

```c
// SDP准备好的回调
static void sess_on_sdp_ready(lws_sess_t* sess, const char* sdp, void* userdata);

// ICE连接建立的回调
static void sess_on_connected(lws_sess_t* sess, void* userdata);

// ICE连接断开的回调
static void sess_on_disconnected(lws_sess_t* sess, const char* reason, void* userdata);
```

### 2.5 实现细节

**UAC流程 (lws_agent_make_call):**

1. 创建dialog
2. 配置并创建媒体会话:
   ```c
   lws_sess_config_t sess_config = {
       .username = agent->config.username,
       // ... 其他配置
   };

   lws_sess_handler_t sess_handler = {
       .on_sdp_ready = sess_on_sdp_ready,
       .on_connected = sess_on_connected,
       .on_disconnected = sess_on_disconnected,
       .userdata = dlg
   };

   dlg->sess = lws_sess_create(&sess_config, &sess_handler);
   ```
3. 开始SDP收集:
   ```c
   lws_sess_gather_candidates(dlg->sess);
   ```
4. **不发送INVITE**，返回dialog
5. 在`sess_on_sdp_ready`回调中:
   - 调用`lws_sess_get_local_sdp(sess)`获取SDP
   - 创建并发送INVITE (复用当前代码中的sip_uac_invite逻辑)

**UAS流程 (sip_uas_oninvite + lws_agent_answer_call):**

1. 在`sip_uas_oninvite`中:
   - 创建dialog
   - 创建媒体会话（同UAC）
   - 提取INVITE中的SDP
   - 调用`lws_sess_set_remote_sdp(dlg->sess, remote_sdp)`
   - 触发`on_incoming_call`回调
   - **不发送200 OK**，等待应用层调用`answer_call`

2. 在`lws_agent_answer_call`中:
   - 调用`lws_sess_gather_candidates(dlg->sess)`
   - **不发送200 OK**，返回成功

3. 在`sess_on_sdp_ready`回调中:
   - 调用`lws_sess_get_local_sdp(sess)`获取SDP
   - 发送200 OK with SDP
   - 调用`lws_sess_start_ice(sess)`启动ICE连接

**收到200 OK (uac_oninvite):**

1. 提取remote SDP
2. 调用`lws_sess_set_remote_sdp(dlg->sess, remote_sdp)`
3. 调用`lws_sess_start_ice(dlg->sess)`
4. 状态更新为CONFIRMED

## 3. 测试策略

使用`tests/lws_sess_stub.c`进行测试：
- Stub会立即触发`on_sdp_ready`回调（模拟同步行为）
- 应该能看到`[MEDIA_SESSION]`日志输出
- 完整的呼叫流程应该能正常工作

## 4. 实现步骤

1. 修改`lws_agent_make_call`:
   - 移除`local_sdp`参数
   - 添加媒体会话创建和SDP收集代码
   - 移除立即发送INVITE的代码

2. 实现`sess_on_sdp_ready`回调:
   - UAC分支: 发送INVITE with SDP
   - UAS分支: 发送200 OK with SDP + start ICE

3. 修改`sip_uas_oninvite`:
   - 添加媒体会话创建代码
   - 调用`set_remote_sdp`
   - 移除发送响应的代码

4. 修改`lws_agent_answer_call`:
   - 移除`local_sdp`参数
   - 调用`gather_candidates`
   - 移除发送200 OK的代码

5. 修改`uac_oninvite` (200 OK处理):
   - 调用`set_remote_sdp`
   - 调用`start_ice`

6. 编译测试

## 5. 向后兼容性

这是一个**破坏性变更**：
- `lws_agent_make_call`和`lws_agent_answer_call`的接口签名改变
- 调用者代码需要修改（tests和cmd中的代码）
- 但这是必需的，因为异步SDP生成是媒体会话层的核心特性
