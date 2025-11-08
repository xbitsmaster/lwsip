# lws_agent.c 修复清单

## 修复优先级：CRITICAL/HIGH (必须立即修复)

### Issue #1: 缺少 sip_uac_onreply 回调实现
**位置**: `src/lws_agent.c:589, 657`
**严重程度**: CRITICAL - 链接错误

**问题**: REGISTER transactions引用了未定义的`sip_uac_onreply`回调

**修复方案**:
```c
/**
 * @brief UAC REGISTER/Un-REGISTER callback
 */
static int sip_uac_onreply(void* param, const struct sip_message_t* reply,
                          struct sip_uac_transaction_t* t, int code, void* context)
{
    lws_agent_t* agent = (lws_agent_t*)param;

    lws_log_info("UAC REGISTER response: %d\n", code);

    lws_mutex_lock(&agent->mutex);

    if (code >= 200 && code < 300) {
        /* 2xx - Success */
        if (agent->state == LWS_AGENT_STATE_REGISTERING) {
            agent->state = LWS_AGENT_STATE_REGISTERED;

            /* Trigger callback */
            if (agent->handler.on_register_result) {
                lws_mutex_unlock(&agent->mutex);
                agent->handler.on_register_result(agent, 1, code, "OK",
                                                 agent->handler.userdata);
                lws_mutex_lock(&agent->mutex);
            }
        }
    } else if (code >= 300) {
        /* Failure */
        agent->state = LWS_AGENT_STATE_REGISTER_FAILED;

        /* Trigger callback */
        if (agent->handler.on_register_result) {
            lws_mutex_unlock(&agent->mutex);
            agent->handler.on_register_result(agent, 0, code, "Failed",
                                             agent->handler.userdata);
            lws_mutex_lock(&agent->mutex);
        }
    }

    /* Release transaction if we stored it */
    if (agent->register_txn == t) {
        sip_uac_transaction_release(agent->register_txn);
        agent->register_txn = NULL;
    }

    lws_mutex_unlock(&agent->mutex);

    return 0;
}
```

---

### Issue #2: 使用不存在的libsip函数 sip_message_get_content
**位置**: `src/lws_agent.c:774`
**严重程度**: CRITICAL - 编译错误

**问题**: libsip没有`sip_message_get_content()`函数

**修复方案**: 直接使用`sip_message_t`的`payload`和`size`字段
```c
// 修改前：
if (sip_message_get_content(reply, &sdp_data, &sdp_len) == 0 && sdp_data) {

// 修改后：
const void* sdp_data = reply->payload;
int sdp_len = reply->size;
if (sdp_data && sdp_len > 0) {
```

---

### Issue #3: Config结构字段名称错误
**位置**: `src/lws_agent.c:240, 266`
**严重程度**: CRITICAL - 编译错误

**问题**: 使用了`server_addr`/`server_port`但实际字段是`registrar`/`registrar_port`

**修复方案**: 全局替换字段名
```bash
# 在lws_agent.c中查找并替换
agent->config.server_addr  → agent->config.registrar
agent->config.server_port  → agent->config.registrar_port
```

---

### Issue #4: 使用未定义的Dialog状态 LWS_DIALOG_STATE_INIT
**位置**: `src/lws_agent.c:165`
**严重程度**: HIGH - 编译错误

**问题**: 枚举中不存在`LWS_DIALOG_STATE_INIT`

**修复方案**:
```c
// 修改前：
dlg->state = LWS_DIALOG_STATE_INIT;

// 修改后：
dlg->state = LWS_DIALOG_STATE_NULL;
```

---

### Issue #5: on_incoming_call回调参数类型错误
**位置**: `src/lws_agent.c:325`
**严重程度**: CRITICAL - 类型不匹配

**问题**: 传递了SDP字符串，但声明要求`const lws_sip_addr_t*`

**修复方案**: 解析From字段到lws_sip_addr_t
```c
// 在调用on_incoming_call之前
lws_sip_addr_t from_addr;
memset(&from_addr, 0, sizeof(from_addr));

// 解析From URI
snprintf(from_addr.username, LWS_MAX_USERNAME_LEN, "%.*s",
         (int)req->from.uri.user.n, req->from.uri.user.p);
snprintf(from_addr.domain, LWS_MAX_DOMAIN_LEN, "%.*s",
         (int)req->from.uri.host.n, req->from.uri.host.p);
from_addr.port = req->from.uri.port;
if (req->from.displayname.n > 0) {
    snprintf(from_addr.nickname, LWS_MAX_NICKNAME_LEN, "%.*s",
             (int)req->from.displayname.n, req->from.displayname.p);
}

// 调用回调
agent->handler.on_incoming_call(agent, &dlg->public, &from_addr,
                               agent->handler.userdata);
```

---

### Issue #6: 引用不存在的回调 on_call_terminated
**位置**: `src/lws_agent.c:361`
**严重程度**: HIGH - 字段不存在

**修复方案**: 使用`on_dialog_state_changed`代替
```c
// 修改前：
if (agent->handler.on_call_terminated) {
    agent->handler.on_call_terminated(agent, &dlg->public,
                                     agent->handler.userdata);
}

// 修改后：
if (agent->handler.on_dialog_state_changed) {
    agent->handler.on_dialog_state_changed(agent, &dlg->public,
                                           dlg->state, LWS_DIALOG_STATE_TERMINATED,
                                           agent->handler.userdata);
}
```

---

### Issue #7: UAS Handler使用static（线程安全问题）
**位置**: `src/lws_agent.c:488`
**严重程度**: MEDIUM-HIGH - 违反多实例原则

**问题**: static使得所有agent实例共享同一个handler

**修复方案**: 改为per-instance
```c
// 在lws_agent_t结构中添加：
struct sip_uas_handler_t uas_handler;

// 在lws_agent_create()中初始化：
agent->uas_handler.send = sip_uas_send;
agent->uas_handler.onregister = sip_uas_onregister;
agent->uas_handler.oninvite = sip_uas_oninvite;
agent->uas_handler.onack = sip_uas_onack;
agent->uas_handler.onbye = sip_uas_onbye;
// ... 其他回调

// 创建sip_agent时使用：
agent->sip_agent = sip_agent_create(&agent->uas_handler);
```

---

### Issue #8: Undefined Timer Functions
**位置**: `src/lws_agent.c:498, 505, 530`
**严重程度**: HIGH - 可能的链接错误

**问题**: `sip_timer_init()`和`sip_timer_cleanup()`可能不存在

**修复方案**:
1. 先验证这些函数是否存在于libsip
2. 如果不存在，移除这些调用（libsip可能自动管理定时器）

---

## 线程安全改造（按用户要求）

### 添加Mutex到lws_agent_t结构

**文件**: `src/lws_agent.c`

```c
struct lws_agent_t {
    /* libsip agent */
    struct sip_agent_t* sip_agent;

    /* Thread safety */
    lws_mutex_t mutex;  // 新增：保护所有共享状态

    /* Transport layer */
    lws_trans_t* trans;
    struct sip_transport_t sip_transport;

    /* UAS handler (per-instance) */
    struct sip_uas_handler_t uas_handler;  // 修改：从static改为per-instance

    /* Configuration */
    lws_agent_config_t config;
    lws_agent_handler_t handler;

    /* Runtime state */
    lws_agent_state_t state;

    /* Dialog management (改用list.h) */
    struct list_head dialogs;  // 修改：从单向链表改为双向链表
    int dialog_count;

    /* SIP registration */
    struct sip_uac_transaction_t* register_txn;
    int register_expires;
};
```

### 保护所有公共API

规则：
- **外部调用的API**（如`lws_agent_start`, `lws_agent_make_call`等）需要加锁
- **回调函数**（如`sip_uas_oninvite`, `uac_oninvite`等）在访问agent状态时需要加锁
- **释放锁后才能调用用户回调**（避免死锁）

示例模式：
```c
int lws_agent_start(lws_agent_t* agent)
{
    if (!agent) {
        return LWS_EINVAL;
    }

    lws_mutex_lock(&agent->mutex);

    // ... 访问agent状态 ...
    agent->state = LWS_AGENT_STATE_REGISTERING;

    // 释放锁后调用libsip（libsip可能调用回调）
    lws_mutex_unlock(&agent->mutex);

    struct sip_uac_transaction_t* reg_txn = sip_uac_register(...);

    lws_mutex_lock(&agent->mutex);
    agent->register_txn = reg_txn;
    lws_mutex_unlock(&agent->mutex);

    return LWS_OK;
}
```

---

## Dialog管理重构（使用list.h）

### 修改lws_dialog_intl_t结构

```c
typedef struct lws_dialog_intl_t {
    lws_dialog_t public;            /**< 公共dialog信息 */

    struct sip_dialog_t* sip_dialog; /**< libsip dialog */
    struct sip_uac_transaction_t* invite_txn; /**< UAC INVITE transaction */
    struct sip_uas_transaction_t* uas_txn;    /**< UAS transaction */

    lws_sess_t* sess;                /**< 媒体会话 */
    lws_agent_t* agent;              /**< 所属agent */

    /* Dialog状态 */
    lws_dialog_state_t state;        /**< Dialog状态 */
    char local_sdp[2048];            /**< 本地SDP */
    char remote_sdp[2048];           /**< 远端SDP */

    /* 链表节点 (新增) */
    struct list_head list;  // 使用list.h的双向链表
} lws_dialog_intl_t;
```

### 修改Dialog管理函数

```c
// 初始化（在lws_agent_create中）
INIT_LIST_HEAD(&agent->dialogs);

// 查找dialog
static lws_dialog_intl_t* lws_agent_find_dialog(lws_agent_t* agent, const char* call_id)
{
    lws_dialog_intl_t* dlg;

    list_for_each_entry(dlg, &agent->dialogs, list) {
        if (strcmp(dlg->public.call_id, call_id) == 0) {
            return dlg;
        }
    }

    return NULL;
}

// 创建dialog
static lws_dialog_intl_t* lws_agent_create_dialog(...)
{
    lws_dialog_intl_t* dlg = lws_calloc(1, sizeof(lws_dialog_intl_t));
    if (!dlg) {
        return NULL;
    }

    // ... 初始化 ...

    // 插入链表
    list_add_tail(&dlg->list, &agent->dialogs);
    agent->dialog_count++;

    return dlg;
}

// 销毁dialog
static void lws_agent_destroy_dialog(lws_agent_t* agent, lws_dialog_intl_t* dlg)
{
    // 从链表移除
    list_del(&dlg->list);
    agent->dialog_count--;

    // ... 清理资源 ...

    lws_free(dlg);
}

// 销毁所有dialogs
while (!list_empty(&agent->dialogs)) {
    lws_dialog_intl_t* dlg = list_first_entry(&agent->dialogs, lws_dialog_intl_t, list);
    lws_agent_destroy_dialog(agent, dlg);
}
```

---

## 修复检查清单

- [ ] 实现`sip_uac_onreply`回调
- [ ] 修复`sip_message_get_content`→使用`reply->payload`
- [ ] 修复`server_addr/server_port`→`registrar/registrar_port`
- [ ] 修复`LWS_DIALOG_STATE_INIT`→`LWS_DIALOG_STATE_NULL`
- [ ] 修复`on_incoming_call`参数类型
- [ ] 修复`on_call_terminated`→使用`on_dialog_state_changed`
- [ ] UAS handler改为per-instance
- [ ] 验证/移除`sip_timer_init/cleanup`调用
- [ ] 添加`lws_mutex_t mutex`到agent结构
- [ ] 添加`struct list_head list`到dialog结构
- [ ] 重构dialog管理使用list.h
- [ ] 在所有公共API中添加互斥锁保护
- [ ] 在回调中添加互斥锁保护（注意死锁）
- [ ] 测试编译通过
- [ ] 测试基本功能

---

## 预估工作量

- 修复8个Critical/High错误: ~200行代码修改
- 添加线程安全保护: ~50行代码修改
- 重构dialog管理: ~100行代码修改
- **总计**: ~350行代码修改

文件：`src/lws_agent.c` (当前1499行) → 预计~1550行
