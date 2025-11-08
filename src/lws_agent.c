/**
 * @file lws_agent.c
 * @brief lwsip SIP Agent implementation
 *
 * lws_agent是lwsip的SIP信令层，作为libsip的高层封装：
 * - 包装libsip的sip_agent_t
 * - 实现sip_transport_t接口，桥接到lws_trans
 * - 实现sip_uas_handler_t回调，处理incoming SIP请求
 * - 管理dialog和对应的media session (lws_sess)
 * - 提供简化的API给应用层使用
 */

#include "lws_intl.h"
#include "lws_agent.h"
#include "lws_sess.h"
#include "lws_defs.h"
#include "lws_err.h"
#include "lws_timer.h"
#include "lws_mem.h"
#include "lws_log.h"

#include <time.h>  /* For time() */

/* libsip headers */
#include "sip-agent.h"
#include "sip-uac.h"
#include "sip-uas.h"
#include "sip-transport.h"
#include "sip-message.h"

/* http parser for SIP message parsing */
#include "http-parser.h"

#include <string.h>
#include <stdio.h>

/* ========================================
 * Internal structures
 * ======================================== */

/**
 * @brief SIP dialog结构 (对应一路呼叫)
 */
typedef struct lws_dialog_intl_t {
    lws_dialog_t public;            /**< 公共dialog信息 */

    struct sip_dialog_t* sip_dialog; /**< libsip dialog */
    struct sip_uac_transaction_t* invite_txn; /**< UAC INVITE transaction */
    struct sip_uas_transaction_t* uas_txn;    /**< UAS transaction (for incoming calls) */

    lws_sess_t* sess;                /**< 媒体会话 */
    lws_agent_t* agent;              /**< 所属agent */

    /* Dialog状态 */
    lws_dialog_state_t state;        /**< Dialog状态 */
    char local_sdp[2048];            /**< 本地SDP */
    char remote_sdp[2048];           /**< 远端SDP */

    /* 链表 */
    struct lws_dialog_intl_t* next;
} lws_dialog_intl_t;

/**
 * @brief SIP Agent结构
 */
struct lws_agent_t {
    /* libsip agent */
    struct sip_agent_t* sip_agent;

    /* Transport layer */
    lws_trans_t* trans;
    struct sip_transport_t sip_transport;  /**< sip_transport_t implementation */

    /* Configuration */
    lws_agent_config_t config;
    lws_agent_handler_t handler;

    /* Runtime state */
    lws_agent_state_t state;

    /* Dialog management */
    lws_dialog_intl_t* dialogs;      /**< Dialog链表 */
    int dialog_count;                 /**< Dialog数量 */

    /* SIP registration */
    struct sip_uac_transaction_t* register_txn;
    int register_expires;
};

/* ========================================
 * Forward declarations
 * ======================================== */

/* Media session callbacks */
static void sess_on_sdp_ready(lws_sess_t* sess, const char* sdp, void* userdata);
static void sess_on_connected(lws_sess_t* sess, void* userdata);
static void sess_on_disconnected(lws_sess_t* sess, const char* reason, void* userdata);

/* UAC callback for REGISTER/unREGISTER responses */
static int uac_onreply_callback(void* param, const struct sip_message_t* reply,
                                struct sip_uac_transaction_t* t, int code);

static int sip_transport_via(void* transport, const char* destination,
                             char protocol[16], char local[128], char dns[128]);
static int sip_transport_send(void* transport, const void* data, size_t bytes);
static int sip_uas_send(void* param, const struct cstring_t* protocol,
                       const struct cstring_t* url, const struct cstring_t* received,
                       int rport, const void* data, int bytes);
static int sip_uas_onregister(void* param, const struct sip_message_t* req,
                              struct sip_uas_transaction_t* t, const char* user,
                              const char* location, int expires);
static int sip_uas_oninvite(void* param, const struct sip_message_t* req,
                            struct sip_uas_transaction_t* t,
                            struct sip_dialog_t* redialog,
                            const struct cstring_t* id,
                            const void* data, int bytes);
static int sip_uas_onack(void* param, const struct sip_message_t* req,
                        struct sip_uas_transaction_t* t,
                        struct sip_dialog_t* dialog,
                        const struct cstring_t* id, int code,
                        const void* data, int bytes);
static int sip_uas_onbye(void* param, const struct sip_message_t* req,
                        struct sip_uas_transaction_t* t,
                        const struct cstring_t* id);

/* ========================================
 * Dialog management
 * ======================================== */

static lws_dialog_intl_t* lws_agent_find_dialog(lws_agent_t* agent, const char* call_id);
static lws_dialog_intl_t* lws_agent_create_dialog(lws_agent_t* agent,
                                                   const char* call_id,
                                                   const char* local_uri,
                                                   const char* remote_uri);
static void lws_agent_destroy_dialog(lws_agent_t* agent, lws_dialog_intl_t* dlg);

static lws_dialog_intl_t* lws_agent_find_dialog(lws_agent_t* agent, const char* call_id)
{
    if (!agent || !call_id) {
        return NULL;
    }

    for (lws_dialog_intl_t* dlg = agent->dialogs; dlg != NULL; dlg = dlg->next) {
        if (strcmp(dlg->public.call_id, call_id) == 0) {
            return dlg;
        }
    }

    return NULL;
}

static lws_dialog_intl_t* lws_agent_create_dialog(lws_agent_t* agent,
                                                   const char* call_id,
                                                   const char* local_uri,
                                                   const char* remote_uri)
{
    if (!agent || !call_id) {
        return NULL;
    }

    lws_dialog_intl_t* dlg = (lws_dialog_intl_t*)lws_calloc(1, sizeof(lws_dialog_intl_t));
    if (!dlg) {
        lws_log_error(LWS_ENOMEM, "Failed to allocate dialog\n");
        return NULL;
    }

    /* 初始化公共信息 */
    LWS_STRNCPY(dlg->public.call_id, call_id, LWS_MAX_CALL_ID_LEN);
    if (local_uri) {
        snprintf(dlg->public.local_uri, sizeof(dlg->public.local_uri), "%s", local_uri);
    }
    if (remote_uri) {
        snprintf(dlg->public.remote_uri, sizeof(dlg->public.remote_uri), "%s", remote_uri);
    }
    dlg->public.direction = LWS_DIALOG_UNKNOWN;

    dlg->agent = agent;
    dlg->state = LWS_DIALOG_STATE_NULL;

    /* 插入链表头 */
    dlg->next = agent->dialogs;
    agent->dialogs = dlg;
    agent->dialog_count++;

    return dlg;
}

static void lws_agent_destroy_dialog(lws_agent_t* agent, lws_dialog_intl_t* dlg)
{
    if (!agent || !dlg) {
        return;
    }

    /* 从链表移除 */
    if (agent->dialogs == dlg) {
        agent->dialogs = dlg->next;
    } else {
        for (lws_dialog_intl_t* p = agent->dialogs; p != NULL; p = p->next) {
            if (p->next == dlg) {
                p->next = dlg->next;
                break;
            }
        }
    }
    agent->dialog_count--;

    /* NOTE: Do NOT manually release invite_txn here!
     * libsip manages the lifecycle of transactions that were successfully sent.
     * Manually releasing would cause double-release and assertion failures.
     * The transaction will be automatically released by libsip when it completes/times out.
     */
    dlg->invite_txn = NULL;  /* Just clear the pointer */

    /* 释放UAS transaction (如果还存在) */
    if (dlg->uas_txn) {
        sip_uas_transaction_release(dlg->uas_txn);
        dlg->uas_txn = NULL;
    }

    /* 销毁media session */
    if (dlg->sess) {
        lws_sess_destroy(dlg->sess);
    }

    lws_free(dlg);
}

/* ========================================
 * Media session callbacks
 * ======================================== */

/**
 * @brief SDP就绪回调 - 当媒体会话层完成ICE candidate收集后调用
 *
 * UAC流程: 在此回调中发送INVITE with SDP
 * UAS流程: 在此回调中发送200 OK with SDP
 */
static void sess_on_sdp_ready(lws_sess_t* sess, const char* sdp, void* userdata)
{
    lws_dialog_intl_t* dlg = (lws_dialog_intl_t*)userdata;

    if (!dlg || !dlg->agent || !sdp) {
        lws_log_error(LWS_EINVAL, "[MEDIA_SESSION] Invalid parameters in sess_on_sdp_ready\n");
        return;
    }

    lws_agent_t* agent = dlg->agent;

    /* 保存本地SDP */
    LWS_STRNCPY(dlg->local_sdp, sdp, sizeof(dlg->local_sdp));

    lws_log_info("[MEDIA_SESSION] SDP ready (%zu bytes)\n", strlen(sdp));

    /* 根据dialog方向决定操作 */
    if (dlg->public.direction == LWS_DIALOG_OUTGOING) {
        /* UAC: 发送INVITE with SDP */
        lws_log_info("[MEDIA_SESSION] UAC sending INVITE with SDP\n");

        /* 添加Content-Type header */
        sip_uac_add_header(dlg->invite_txn, "Content-Type", "application/sdp");

        /* 发送INVITE */
        int ret = sip_uac_send(dlg->invite_txn, sdp, strlen(sdp),
                              &agent->sip_transport, agent);
        if (ret < 0) {
            lws_log_error(LWS_ERR_SIP_SEND, "[MEDIA_SESSION] Failed to send INVITE: %d\n", ret);
            /* TODO: 清理并通知应用层 */
        } else {
            lws_log_info("INVITE sent with SDP\n");
        }

    } else if (dlg->public.direction == LWS_DIALOG_INCOMING) {
        /* UAS: 发送200 OK with SDP */
        lws_log_info("[MEDIA_SESSION] UAS sending 200 OK with SDP\n");

        if (!dlg->uas_txn) {
            lws_log_error(LWS_ERROR, "[MEDIA_SESSION] No UAS transaction\n");
            return;
        }

        /* 添加Content-Type header */
        sip_uas_add_header(dlg->uas_txn, "Content-Type", "application/sdp");

        /* 发送200 OK */
        int ret = sip_uas_reply(dlg->uas_txn, 200, sdp, strlen(sdp), agent);
        if (ret < 0) {
            lws_log_error(LWS_ERR_SIP_SEND, "[MEDIA_SESSION] Failed to send 200 OK\n");
            return;
        }

        /* 更新dialog状态 */
        lws_dialog_state_t old_state = dlg->state;
        dlg->state = LWS_DIALOG_STATE_CONFIRMED;

        /* 触发回调 */
        if (agent->handler.on_dialog_state_changed) {
            agent->handler.on_dialog_state_changed(agent, (lws_dialog_t*)dlg,
                                                   old_state, LWS_DIALOG_STATE_CONFIRMED,
                                                   agent->handler.userdata);
        }

        /* 启动ICE连接 */
        if (lws_sess_start_ice(sess) < 0) {
            lws_log_error(LWS_ERROR, "[MEDIA_SESSION] Failed to start ICE\n");
        }

        /* 释放UAS transaction */
        sip_uas_transaction_release(dlg->uas_txn);
        dlg->uas_txn = NULL;

        lws_log_info("[MEDIA_SESSION] Call answered, sent 200 OK with SDP\n");
    }
}

/**
 * @brief 媒体连接建立回调
 */
static void sess_on_connected(lws_sess_t* sess, void* userdata)
{
    lws_dialog_intl_t* dlg = (lws_dialog_intl_t*)userdata;
    LWS_UNUSED(sess);

    if (!dlg) {
        return;
    }

    lws_log_info("[MEDIA_SESSION] Media connected (ICE connection established)\n");

    /* TODO: 可以在这里通知应用层媒体已连接 */
}

/**
 * @brief 媒体连接断开回调
 */
static void sess_on_disconnected(lws_sess_t* sess, const char* reason, void* userdata)
{
    lws_dialog_intl_t* dlg = (lws_dialog_intl_t*)userdata;
    LWS_UNUSED(sess);

    if (!dlg) {
        return;
    }

    lws_log_info("[MEDIA_SESSION] Media disconnected (reason: %s)\n", reason ? reason : "unknown");

    /* TODO: 可以在这里通知应用层媒体已断开 */
}

/* ========================================
 * sip_transport_t implementation
 * ======================================== */

static int sip_transport_via(void* transport, const char* destination,
                             char protocol[16], char local[128], char dns[128])
{
    lws_agent_t* agent = (lws_agent_t*)transport;

    /* 协议固定为UDP */
    snprintf(protocol, 16, "UDP");

    /* 获取本地地址 */
    lws_addr_t local_addr;
    if (lws_trans_get_local_addr(agent->trans, &local_addr) == LWS_OK) {
        snprintf(local, 128, "%s:%d", local_addr.ip, local_addr.port);
        snprintf(dns, 128, "%s:%d", local_addr.ip, local_addr.port);
    } else {
        /* Fallback */
        snprintf(local, 128, "0.0.0.0:5060");
        snprintf(dns, 128, "0.0.0.0:5060");
    }

    return 0;
}

static int sip_transport_send(void* transport, const void* data, size_t bytes)
{
    lws_agent_t* agent = (lws_agent_t*)transport;

    /* 发送到registrar server */
    lws_addr_t to;

    /* Parse registrar address - may be "host" or "host:port" format */
    const char* colon = strchr(agent->config.registrar, ':');
    if (colon) {
        /* Format: "host:port" - extract host and port */
        size_t host_len = colon - agent->config.registrar;
        if (host_len >= sizeof(to.ip)) {
            host_len = sizeof(to.ip) - 1;
        }
        memcpy(to.ip, agent->config.registrar, host_len);
        to.ip[host_len] = '\0';
        to.port = atoi(colon + 1);
    } else {
        /* Format: "host" only - use default port */
        snprintf(to.ip, sizeof(to.ip), "%s", agent->config.registrar);
        to.port = agent->config.registrar_port ? agent->config.registrar_port : 5060;
    }

    int sent = lws_trans_send(agent->trans, data, (int)bytes, &to);
    if (sent < 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "Failed to send SIP message\n");
        return -1;
    }

    return 0;
}

/* ========================================
 * sip_uas_handler_t callbacks
 * ======================================== */

static int sip_uas_send(void* param, const struct cstring_t* protocol,
                       const struct cstring_t* url, const struct cstring_t* received,
                       int rport, const void* data, int bytes)
{
    lws_agent_t* agent = (lws_agent_t*)param;

    lws_addr_t to;

    /* Use received+rport if available (RFC 3581 rport) */
    if (received && received->p && received->n > 0) {
        /* Use received IP from Via header */
        snprintf(to.ip, sizeof(to.ip), "%.*s", (int)received->n, received->p);
        to.port = (rport > 0) ? rport : 5060;
    } else if (url && url->p && url->n > 0) {
        /* Parse URL to extract IP:port */
        char url_str[256];
        snprintf(url_str, sizeof(url_str), "%.*s", (int)url->n, url->p);

        /* Simple parsing: look for IP:port pattern */
        const char* colon = strchr(url_str, ':');
        if (colon && *(colon-1) != '/') {  /* Avoid matching "sip:" */
            const char* ip_start = url_str;
            /* Find IP start (skip "sip:" prefix if present) */
            const char* at_sign = strchr(url_str, '@');
            if (at_sign) {
                ip_start = at_sign + 1;
            }

            const char* port_colon = strrchr(ip_start, ':');
            if (port_colon) {
                snprintf(to.ip, sizeof(to.ip), "%.*s", (int)(port_colon - ip_start), ip_start);
                to.port = atoi(port_colon + 1);
            } else {
                snprintf(to.ip, sizeof(to.ip), "%s", ip_start);
                to.port = 5060;
            }
        } else {
            /* No port, use default */
            snprintf(to.ip, sizeof(to.ip), "%.*s", (int)url->n, url->p);
            to.port = 5060;
        }
    } else {
        /* Fallback: send to registrar */
        snprintf(to.ip, sizeof(to.ip), "%s", agent->config.registrar);
        to.port = agent->config.registrar_port ? agent->config.registrar_port : 5060;
    }

    int sent = lws_trans_send(agent->trans, data, bytes, &to);
    if (sent < 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "Failed to send UAS response\n");
        return -1;
    }

    return 0;
}

static int sip_uas_onregister(void* param, const struct sip_message_t* req,
                              struct sip_uas_transaction_t* t, const char* user,
                              const char* location, int expires)
{
    lws_agent_t* agent = (lws_agent_t*)param;

    /* Not supported as UAS (we're UAC only for now) */
    sip_uas_reply(t, 405, NULL, 0, param);  /* Method Not Allowed */

    return 0;
}

static int sip_uas_oninvite(void* param, const struct sip_message_t* req,
                            struct sip_uas_transaction_t* t,
                            struct sip_dialog_t* redialog,
                            const struct cstring_t* id,
                            const void* data, int bytes)
{
    lws_agent_t* agent = (lws_agent_t*)param;

    /* Extract call-ID */
    char call_id[LWS_MAX_CALL_ID_LEN];
    snprintf(call_id, sizeof(call_id), "%.*s", (int)req->callid.n, req->callid.p);

    /* Create dialog */
    lws_dialog_intl_t* dlg = lws_agent_create_dialog(agent, call_id, NULL, NULL);
    if (!dlg) {
        sip_uas_reply(t, 500, NULL, 0, param);  /* Internal Server Error */
        return -1;
    }

    dlg->sip_dialog = redialog;
    dlg->state = LWS_DIALOG_STATE_INCOMING;
    dlg->public.direction = LWS_DIALOG_INCOMING;

    /* 保存UAS transaction (增加引用计数) */
    sip_uas_transaction_addref(t);
    dlg->uas_txn = t;

    /* 保存remote SDP */
    if (data && bytes > 0) {
        int len = LWS_MIN(bytes, (int)sizeof(dlg->remote_sdp) - 1);
        memcpy(dlg->remote_sdp, data, len);
        dlg->remote_sdp[len] = '\0';
    }

    /* 创建媒体会话 (UAS) */
    lws_sess_config_t sess_config;
    memset(&sess_config, 0, sizeof(sess_config));
    sess_config.enable_audio = 1;
    sess_config.enable_video = 0;
    /* TODO: 可以从agent config中获取STUN/TURN服务器配置 */

    lws_sess_handler_t sess_handler;
    memset(&sess_handler, 0, sizeof(sess_handler));
    sess_handler.on_sdp_ready = sess_on_sdp_ready;
    sess_handler.on_connected = sess_on_connected;
    sess_handler.on_disconnected = sess_on_disconnected;
    sess_handler.userdata = dlg;

    dlg->sess = lws_sess_create(&sess_config, &sess_handler);
    if (!dlg->sess) {
        lws_log_error(LWS_ERR_MEDIA_SESSION, "Failed to create media session for UAS\n");
        lws_agent_destroy_dialog(agent, dlg);
        sip_uas_reply(t, 500, NULL, 0, param);  /* Internal Server Error */
        return -1;
    }

    lws_log_info("[MEDIA_SESSION] Media session created for incoming call (Call-ID: %s)\n", call_id);

    /* 设置远程SDP到媒体会话层 (如果INVITE包含SDP) */
    if (data && bytes > 0) {
        int ret = lws_sess_set_remote_sdp(dlg->sess, dlg->remote_sdp);
        if (ret != 0) {
            lws_log_error(LWS_ERR_MEDIA_SESSION, "Failed to set remote SDP for UAS\n");
            lws_agent_destroy_dialog(agent, dlg);
            sip_uas_reply(t, 488, NULL, 0, param);  /* Not Acceptable Here */
            return -1;
        }
        lws_log_info("[MEDIA_SESSION] Remote SDP set from INVITE\n");
    }

    /* 解析From地址 */
    lws_sip_addr_t from_addr;
    memset(&from_addr, 0, sizeof(from_addr));

    /* 解析From URI */
    snprintf(from_addr.username, LWS_MAX_USERNAME_LEN, "%.*s",
             (int)req->from.uri.user.n, req->from.uri.user.p);
    snprintf(from_addr.domain, LWS_MAX_DOMAIN_LEN, "%.*s",
             (int)req->from.uri.host.n, req->from.uri.host.p);
    from_addr.port = 0;  /* Port is embedded in host string, use default */
    if (req->from.nickname.n > 0) {
        snprintf(from_addr.nickname, LWS_MAX_NICKNAME_LEN, "%.*s",
                 (int)req->from.nickname.n, req->from.nickname.p);
    }

    /* 通知应用层 */
    if (agent->handler.on_incoming_call) {
        agent->handler.on_incoming_call(agent, &dlg->public, &from_addr,
                                       agent->handler.userdata);
    }

    /* 应用层应该调用lws_agent_answer_call()来应答，或lws_agent_reject_call()拒绝 */
    /* 不在这里自动回复，让应用层决定 */

    return 0;
}

static int sip_uas_onack(void* param, const struct sip_message_t* req,
                        struct sip_uas_transaction_t* t,
                        struct sip_dialog_t* dialog,
                        const struct cstring_t* id, int code,
                        const void* data, int bytes)
{
    /* ACK received, call established */
    return 0;
}

static int sip_uas_onbye(void* param, const struct sip_message_t* req,
                        struct sip_uas_transaction_t* t,
                        const struct cstring_t* id)
{
    lws_agent_t* agent = (lws_agent_t*)param;

    /* Extract call-ID */
    char call_id[LWS_MAX_CALL_ID_LEN];
    snprintf(call_id, sizeof(call_id), "%.*s", (int)req->callid.n, req->callid.p);

    lws_log_info("Received BYE request (Call-ID: %s)\n", call_id);
    lws_log_info("[MEDIA_SESSION] Media session terminated (UAS received BYE)\n");

    /* Find dialog */
    lws_dialog_intl_t* dlg = lws_agent_find_dialog(agent, call_id);
    if (dlg) {
        lws_dialog_state_t old_state = dlg->state;
        dlg->state = LWS_DIALOG_STATE_TERMINATED;

        /* 通知应用层 */
        if (agent->handler.on_dialog_state_changed) {
            agent->handler.on_dialog_state_changed(agent, &dlg->public,
                                                   old_state, LWS_DIALOG_STATE_TERMINATED,
                                                   agent->handler.userdata);
        }

        /* 销毁dialog */
        lws_agent_destroy_dialog(agent, dlg);
    }

    /* Send 200 OK */
    sip_uas_reply(t, 200, NULL, 0, param);

    return 0;
}

/* ========================================
 * Transport callbacks
 * ======================================== */

static void trans_on_data(lws_trans_t* trans, const void* data, int len,
                         const lws_addr_t* from, void* userdata)
{
    lws_agent_t* agent = (lws_agent_t*)userdata;
    LWS_UNUSED(trans);

    lws_log_info("[DEBUG] trans_on_data: Received %d bytes from %s:%d\n", len, from->ip, from->port);

    /* Detect message type: response starts with "SIP/2.0", request does not */
    int is_response = (len >= 7 && memcmp(data, "SIP/2.0", 7) == 0);

    lws_log_info("[DEBUG] trans_on_data: is_response=%d\n", is_response);

    /* Print first line of message for debugging */
    const char* msg_str = (const char*)data;
    int first_line_len = 0;
    for (int i = 0; i < len && i < 200; i++) {
        if (msg_str[i] == '\r' || msg_str[i] == '\n') {
            first_line_len = i;
            break;
        }
    }
    if (first_line_len > 0) {
        lws_log_info("[DEBUG] trans_on_data: First line: %.*s\n", first_line_len, msg_str);
    }

    lws_log_debug("Received %d bytes from %s:%d\n", len, from->ip, from->port);

    /* Parse SIP message */
    http_parser_t* parser = http_parser_create(
        is_response ? HTTP_PARSER_RESPONSE : HTTP_PARSER_REQUEST, NULL, NULL);
    if (!parser) {
        lws_log_error(LWS_ENOMEM, "Failed to create HTTP parser\n");
        return;
    }

    /* Feed data to parser */
    size_t bytes = (size_t)len;
    int ret = http_parser_input(parser, data, &bytes);
    if (ret < 0) {
        lws_log_error(LWS_ERR_SIP_PARSE, "Failed to parse SIP message\n");
        http_parser_destroy(parser);
        return;
    }

    /* Create SIP message structure */
    struct sip_message_t* msg = sip_message_create(
        is_response ? SIP_MESSAGE_REPLY : SIP_MESSAGE_REQUEST);
    if (!msg) {
        lws_log_error(LWS_ENOMEM, "Failed to create SIP message\n");
        http_parser_destroy(parser);
        return;
    }

    /* Load parsed data into SIP message */
    ret = sip_message_load(msg, parser);
    if (ret < 0) {
        lws_log_error(LWS_ERR_SIP_PARSE, "Failed to load SIP message\n");
        sip_message_destroy(msg);
        http_parser_destroy(parser);
        return;
    }

    /* Set rport if needed (for NAT traversal) */
    sip_agent_set_rport(msg, from->ip, from->port);

    /* Feed to SIP agent */
    ret = sip_agent_input(agent->sip_agent, msg, agent);
    if (ret < 0) {
        lws_log_error(LWS_ERROR, "sip_agent_input failed: %d\n", ret);
    }

    /* Cleanup - sip_agent_input() takes ownership of msg */
    http_parser_destroy(parser);
}

static void trans_on_error(lws_trans_t* trans, int error, const char* msg,
                          void* userdata)
{
    lws_log_error(error, "Transport error: %s\n", msg);
}

/* ========================================
 * Public API implementation
 * ======================================== */

lws_agent_t* lws_agent_create(const lws_agent_config_t* config,
                              const lws_agent_handler_t* handler)
{
    if (!config || !handler) {
        lws_log_error(LWS_EINVAL, "Invalid parameters\n");
        return NULL;
    }

    lws_agent_t* agent = (lws_agent_t*)lws_calloc(1, sizeof(lws_agent_t));
    if (!agent) {
        lws_log_error(LWS_ENOMEM, "Failed to allocate agent\n");
        return NULL;
    }

    /* Save config and handler */
    agent->config = *config;
    agent->handler = *handler;
    agent->state = LWS_AGENT_STATE_IDLE;

    /* Create transport */
    lws_trans_config_t trans_config = {0};
    trans_config.type = LWS_TRANS_TYPE_UDP;
    trans_config.sock.bind_addr[0] = '\0';  /* Bind to any */
    trans_config.sock.bind_port = 0;         /* Use random port */
    trans_config.sock.reuse_addr = 1;

    lws_trans_handler_t trans_handler = {
        .on_data = trans_on_data,
        .on_error = trans_on_error,
        .userdata = agent
    };

    agent->trans = lws_trans_create(&trans_config, &trans_handler);
    if (!agent->trans) {
        lws_log_error(LWS_ERR_TRANS_CREATE, "Failed to create transport\n");
        lws_free(agent);
        return NULL;
    }

    /* Setup sip_transport */
    agent->sip_transport.via = sip_transport_via;
    agent->sip_transport.send = sip_transport_send;

    /* Setup UAS handler */
    static struct sip_uas_handler_t uas_handler = {
        .send = sip_uas_send,
        .onregister = sip_uas_onregister,
        .oninvite = sip_uas_oninvite,
        .onack = sip_uas_onack,
        .onbye = sip_uas_onbye,
        /* TODO: Add other callbacks */
    };

    /* Initialize libsip timer system (global resource) */
    lws_timer_init();

    /* Create libsip agent */
    agent->sip_agent = sip_agent_create(&uas_handler);
    if (!agent->sip_agent) {
        lws_log_error(LWS_ERR_SIP_CREATE, "Failed to create SIP agent\n");
        lws_trans_destroy(agent->trans);
        lws_free(agent);
        return NULL;
    }

    return agent;
}

void lws_agent_destroy(lws_agent_t* agent)
{
    if (!agent) {
        return;
    }

    /* Destroy all dialogs */
    while (agent->dialogs) {
        lws_agent_destroy_dialog(agent, agent->dialogs);
    }

    /* Destroy libsip agent */
    if (agent->sip_agent) {
        sip_agent_destroy(agent->sip_agent);
    }

    /* Cleanup libsip timer system (global resource) */
    lws_timer_cleanup();

    /* Destroy transport */
    if (agent->trans) {
        lws_trans_destroy(agent->trans);
    }

    lws_free(agent);
}

int lws_agent_start(lws_agent_t* agent)
{
    if (!agent) {
        return LWS_EINVAL;
    }

    /* Build AOR (Address of Record) - e.g., "sip:user@domain" */
    char aor[LWS_MAX_URI_LEN];
    snprintf(aor, sizeof(aor), "sip:%s@%s",
             agent->config.username, agent->config.domain);

    /* Build registrar URI if configured, otherwise use domain */
    char registrar[LWS_MAX_URI_LEN];
    if (agent->config.registrar[0] != '\0') {
        if (agent->config.registrar_port != 0 && agent->config.registrar_port != 5060) {
            snprintf(registrar, sizeof(registrar), "sip:%s:%d",
                     agent->config.registrar, agent->config.registrar_port);
        } else {
            snprintf(registrar, sizeof(registrar), "sip:%s", agent->config.registrar);
        }
    } else {
        /* Use domain as registrar */
        snprintf(registrar, sizeof(registrar), "sip:%s", agent->config.domain);
    }

    /* Get expires time from config (default 3600 seconds) */
    int expires = agent->config.register_expires > 0 ?
                  agent->config.register_expires : 3600;

    /* Update state to REGISTERING */
    lws_agent_state_t old_state = agent->state;
    agent->state = LWS_AGENT_STATE_REGISTERING;

    /* Trigger state change callback */
    if (agent->handler.on_state_changed) {
        agent->handler.on_state_changed(agent, old_state,
                                       LWS_AGENT_STATE_REGISTERING,
                                       agent->handler.userdata);
    }

    /* Send REGISTER request
     * libsip will handle authentication challenges (401/407) automatically
     * The response will be received in sip_uac_onreply callback
     */
    struct sip_uac_transaction_t* reg_txn = sip_uac_register(
        agent->sip_agent,
        aor,
        registrar,
        expires,
        uac_onreply_callback,
        agent
    );

    if (!reg_txn) {
        lws_log_error(LWS_ERR_SIP_REGISTER, "Failed to create REGISTER transaction\n");
        agent->state = LWS_AGENT_STATE_REGISTER_FAILED;

        if (agent->handler.on_state_changed) {
            agent->handler.on_state_changed(agent, LWS_AGENT_STATE_REGISTERING,
                                           LWS_AGENT_STATE_REGISTER_FAILED,
                                           agent->handler.userdata);
        }

        return LWS_ERR_SIP_REGISTER;
    }

    /* Actually send the REGISTER message
     * According to libsip API (sip-uac.h:67), after creating a transaction
     * we must call sip_uac_send() to actually send the message
     */
    int ret = sip_uac_send(reg_txn, NULL, 0, &agent->sip_transport, agent);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "Failed to send REGISTER message: %d\n", ret);
        sip_uac_transaction_release(reg_txn);  /* Release on send failure */
        agent->state = LWS_AGENT_STATE_REGISTER_FAILED;

        if (agent->handler.on_state_changed) {
            agent->handler.on_state_changed(agent, LWS_AGENT_STATE_REGISTERING,
                                           LWS_AGENT_STATE_REGISTER_FAILED,
                                           agent->handler.userdata);
        }

        return LWS_ERR_SIP_SEND;
    }

    /* Store the registration transaction (will be released in callback) */
    agent->register_txn = reg_txn;

    lws_log_info("REGISTER sent to %s (expires: %d)\n", registrar, expires);

    return LWS_OK;
}

int lws_agent_stop(lws_agent_t* agent)
{
    if (!agent) {
        return LWS_EINVAL;
    }

    /* Build AOR (Address of Record) */
    char aor[LWS_MAX_URI_LEN];
    snprintf(aor, sizeof(aor), "sip:%s@%s",
             agent->config.username, agent->config.domain);

    /* Build registrar URI */
    char registrar[LWS_MAX_URI_LEN];
    if (agent->config.registrar[0] != '\0') {
        if (agent->config.registrar_port != 0 && agent->config.registrar_port != 5060) {
            snprintf(registrar, sizeof(registrar), "sip:%s:%d",
                     agent->config.registrar, agent->config.registrar_port);
        } else {
            snprintf(registrar, sizeof(registrar), "sip:%s", agent->config.registrar);
        }
    } else {
        snprintf(registrar, sizeof(registrar), "sip:%s", agent->config.domain);
    }

    /* Update state to UNREGISTERING */
    lws_agent_state_t old_state = agent->state;
    agent->state = LWS_AGENT_STATE_UNREGISTERING;

    /* Trigger state change callback */
    if (agent->handler.on_state_changed) {
        agent->handler.on_state_changed(agent, old_state,
                                       LWS_AGENT_STATE_UNREGISTERING,
                                       agent->handler.userdata);
    }

    /* Send REGISTER with Expires: 0 to unregister
     * This tells the server to remove the registration
     */
    struct sip_uac_transaction_t* unreg_txn = sip_uac_register(
        agent->sip_agent,
        aor,
        registrar,
        0,  /* Expires: 0 means unregister */
        uac_onreply_callback,
        agent
    );

    if (!unreg_txn) {
        lws_log_error(LWS_ERROR, "Failed to create un-REGISTER transaction\n");
        /* Still update state to unregistered even if request failed */
        agent->state = LWS_AGENT_STATE_UNREGISTERED;

        if (agent->handler.on_state_changed) {
            agent->handler.on_state_changed(agent, LWS_AGENT_STATE_UNREGISTERING,
                                           LWS_AGENT_STATE_UNREGISTERED,
                                           agent->handler.userdata);
        }

        return LWS_ERROR;
    }

    /* Send the un-REGISTER transaction */
    int ret = sip_uac_send(unreg_txn, NULL, 0, &agent->sip_transport, agent);
    if (ret < 0) {
        lws_log_error(LWS_ERROR, "Failed to send un-REGISTER: %d\n", ret);
        sip_uac_transaction_release(unreg_txn);
        /* Still update state to unregistered even if send failed */
        agent->state = LWS_AGENT_STATE_UNREGISTERED;

        if (agent->handler.on_state_changed) {
            agent->handler.on_state_changed(agent, LWS_AGENT_STATE_UNREGISTERING,
                                           LWS_AGENT_STATE_UNREGISTERED,
                                           agent->handler.userdata);
        }

        return LWS_ERROR;
    }

    /* Release the un-registration transaction immediately (fire-and-forget) */
    sip_uac_transaction_release(unreg_txn);

    lws_log_info("Un-REGISTER sent to %s\n", registrar);

    /* Update state to UNREGISTERED */
    agent->state = LWS_AGENT_STATE_UNREGISTERED;

    if (agent->handler.on_state_changed) {
        agent->handler.on_state_changed(agent, LWS_AGENT_STATE_UNREGISTERING,
                                       LWS_AGENT_STATE_UNREGISTERED,
                                       agent->handler.userdata);
    }

    return LWS_OK;
}

int lws_agent_loop(lws_agent_t* agent, int timeout_ms)
{
    if (!agent) {
        return LWS_EINVAL;
    }

    /*
     * SIP Timer Processing Strategy:
     *
     * libsip uses a global timer system (sip_timer_start/stop) that manages
     * all SIP transaction timers (Timer A-K per RFC 3261). Timers are fired
     * automatically by the system when they expire.
     *
     * Our event loop strategy:
     * 1. Use a bounded timeout (max 500ms) to ensure we don't block indefinitely
     * 2. This allows timer callbacks to be processed periodically
     * 3. The transport loop will receive SIP messages which trigger sip_agent_input()
     * 4. sip_agent_input() processes messages and updates the SIP state machine
     *
     * Note: libsip's timer system is event-driven - timers are processed when
     * they expire, not by explicit polling. The bounded timeout ensures we
     * return control regularly so the system can process expired timers.
     */

    /* Limit timeout to 500ms to ensure regular timer processing */
    int bounded_timeout = (timeout_ms <= 0 || timeout_ms > 500) ? 500 : timeout_ms;

    /* Drive transport layer */
    int ret = lws_trans_loop(agent->trans, bounded_timeout);
    if (ret < 0) {
        return ret;
    }

    return LWS_OK;
}

/* ========================================
 * UAC callbacks
 * ======================================== */

/**
 * @brief UAC REGISTER callback - handles responses to REGISTER/Un-REGISTER
 */
static int uac_onreply_callback(void* param, const struct sip_message_t* reply,
                          struct sip_uac_transaction_t* t, int code)
{
    lws_agent_t* agent = (lws_agent_t*)param;
    LWS_UNUSED(reply);

    lws_log_info("[DEBUG] uac_onreply_callback called: param=%p, code=%d\n", param, code);

    if (!agent) {
        lws_log_error(LWS_EINVAL, "[DEBUG] uac_onreply_callback: agent is NULL!\n");
        return -1;
    }

    lws_log_info("[DEBUG] UAC REGISTER response: %d, agent state=%d\n", code, agent->state);
    lws_log_info("UAC REGISTER response: %d\n", code);

    if (code >= 200 && code < 300) {
        /* 2xx - Success */
        if (agent->state == LWS_AGENT_STATE_REGISTERING) {
            agent->state = LWS_AGENT_STATE_REGISTERED;

            /* Trigger callback */
            if (agent->handler.on_register_result) {
                agent->handler.on_register_result(agent, 1, code, "OK",
                                                 agent->handler.userdata);
            }
        }
    } else if (code >= 300) {
        /* Failure */
        if (agent->state == LWS_AGENT_STATE_REGISTERING) {
            agent->state = LWS_AGENT_STATE_REGISTER_FAILED;

            /* Trigger callback */
            if (agent->handler.on_register_result) {
                agent->handler.on_register_result(agent, 0, code, "Failed",
                                                 agent->handler.userdata);
            }
        }
    }

    /* Release transaction if we stored it */
    if (agent->register_txn == t) {
        sip_uac_transaction_release(agent->register_txn);
        agent->register_txn = NULL;
    }

    return 0;
}

/**
 * @brief UAC INVITE callback - handles responses to INVITE
 */
static int uac_oninvite(void* param, const struct sip_message_t* reply,
                       struct sip_uac_transaction_t* t,
                       struct sip_dialog_t* dialog, const struct cstring_t* id,
                       int code)
{
    lws_dialog_intl_t* dlg = (lws_dialog_intl_t*)param;
    LWS_UNUSED(reply);
    LWS_UNUSED(t);
    LWS_UNUSED(id);

    if (!dlg || !dlg->agent) {
        return -1;
    }

    lws_agent_t* agent = dlg->agent;

    lws_log_info("UAC INVITE response: %d\n", code);

    /* Update dialog state based on response code */
    lws_dialog_state_t old_state = dlg->state;

    if (code >= 100 && code < 200) {
        /* 1xx - Provisional responses */
        dlg->state = LWS_DIALOG_STATE_EARLY;

        if (code == 180 || code == 183) {
            lws_log_info("Call is ringing/progress (code=%d)\n", code);
        }
    }
    else if (code >= 200 && code < 300) {
        /* 2xx - Success */
        dlg->state = LWS_DIALOG_STATE_CONFIRMED;
        dlg->sip_dialog = dialog;

        lws_log_info("Call answered successfully (200 OK)\n");

        /* Extract remote SDP from 200 OK response */
        const void* sdp_data = reply->payload;
        int sdp_len = reply->size;
        if (sdp_data && sdp_len > 0) {
            int copy_len = sdp_len < (int)sizeof(dlg->remote_sdp) - 1 ?
                          sdp_len : (int)sizeof(dlg->remote_sdp) - 1;
            memcpy(dlg->remote_sdp, sdp_data, copy_len);
            dlg->remote_sdp[copy_len] = '\0';

            lws_log_debug("Received remote SDP (%d bytes)\n", sdp_len);

            /* 设置远程SDP到媒体会话层并启动ICE */
            if (dlg->sess) {
                int ret = lws_sess_set_remote_sdp(dlg->sess, dlg->remote_sdp);
                if (ret != 0) {
                    lws_log_error(LWS_ERR_MEDIA_SESSION,
                                 "Failed to set remote SDP for UAC (ret=%d)\n", ret);
                } else {
                    lws_log_info("[MEDIA_SESSION] Remote SDP set successfully (UAC)\n");

                    /* 启动ICE连接 */
                    ret = lws_sess_start_ice(dlg->sess);
                    if (ret != 0) {
                        lws_log_error(LWS_ERR_MEDIA_SESSION,
                                     "Failed to start ICE for UAC (ret=%d)\n", ret);
                    } else {
                        lws_log_info("[MEDIA_SESSION] Starting ICE negotiation (UAC received 200 OK)\n");
                    }
                }
            } else {
                lws_log_warn(LWS_ERROR, "No media session available for UAC\n");
            }
        }

        /* Send ACK for 200 OK */
        /* Note: libsip automatically handles ACK for 2xx, we just need to
         * call sip_uac_ack if we have additional SDP to send */
    }
    else if (code >= 300) {
        /* 3xx, 4xx, 5xx, 6xx - Failure */
        dlg->state = LWS_DIALOG_STATE_FAILED;

        lws_log_warn(LWS_ERR_SIP_CALL, "Call failed with code %d\n", code);
    }

    /* Trigger state change callback */
    if (old_state != dlg->state && agent->handler.on_dialog_state_changed) {
        agent->handler.on_dialog_state_changed(agent, (lws_dialog_t*)dlg,
                                               old_state, dlg->state,
                                               agent->handler.userdata);
    }

    /* Trigger remote SDP callback for 200 OK */
    if (code >= 200 && code < 300 && agent->handler.on_remote_sdp) {
        if (strlen(dlg->remote_sdp) > 0) {
            lws_log_info("[MEDIA_SESSION] Media session established (UAC received 200 OK + SDP)\n");
            agent->handler.on_remote_sdp(agent, (lws_dialog_t*)dlg,
                                        dlg->remote_sdp,
                                        agent->handler.userdata);
        }
    }

    return 0;
}

/* ========================================
 * Call control implementations
 * ======================================== */

lws_dialog_t* lws_agent_make_call(lws_agent_t* agent, const char* target_uri)
{
    if (!agent || !target_uri) {
        lws_log_error(LWS_EINVAL, "Invalid parameters\n");
        return NULL;
    }

    /* Generate a unique Call-ID for this dialog */
    char call_id[LWS_MAX_CALL_ID_LEN];
    snprintf(call_id, sizeof(call_id), "%08x-%08x",
             (unsigned int)time(NULL), (unsigned int)rand());

    /* Build local URI from agent config */
    char local_uri[256];
    snprintf(local_uri, sizeof(local_uri), "sip:%s@%s",
             agent->config.username, agent->config.domain);

    /* Create dialog */
    lws_dialog_intl_t* dlg = lws_agent_create_dialog(agent, call_id,
                                                     local_uri, target_uri);
    if (!dlg) {
        lws_log_error(LWS_ENOMEM, "Failed to create dialog\n");
        return NULL;
    }

    /* Set dialog direction and initial state */
    dlg->public.direction = LWS_DIALOG_OUTGOING;
    dlg->state = LWS_DIALOG_STATE_CALLING;

    /* Build From header (local URI with nickname) */
    char from[512];
    if (strlen(agent->config.nickname) > 0) {
        snprintf(from, sizeof(from), "\"%s\" <%s>",
                agent->config.nickname, local_uri);
    } else {
        snprintf(from, sizeof(from), "<%s>", local_uri);
    }

    /* Create UAC INVITE transaction (but don't send yet - wait for SDP) */
    dlg->invite_txn = sip_uac_invite(agent->sip_agent, from, target_uri,
                                    uac_oninvite, dlg);
    if (!dlg->invite_txn) {
        lws_log_error(LWS_ERR_SIP_CALL, "Failed to create INVITE transaction\n");
        lws_agent_destroy_dialog(agent, dlg);
        return NULL;
    }

    /* 创建媒体会话 */
    lws_sess_config_t sess_config;
    memset(&sess_config, 0, sizeof(sess_config));
    /* TODO: 配置STUN服务器等参数 */
    sess_config.enable_audio = 1;
    sess_config.enable_video = 0;

    lws_sess_handler_t sess_handler;
    memset(&sess_handler, 0, sizeof(sess_handler));
    sess_handler.on_sdp_ready = sess_on_sdp_ready;
    sess_handler.on_connected = sess_on_connected;
    sess_handler.on_disconnected = sess_on_disconnected;
    sess_handler.userdata = dlg;

    dlg->sess = lws_sess_create(&sess_config, &sess_handler);
    if (!dlg->sess) {
        lws_log_error(LWS_ERR_MEDIA, "Failed to create media session\n");
        sip_uac_transaction_release(dlg->invite_txn);
        dlg->invite_txn = NULL;
        lws_agent_destroy_dialog(agent, dlg);
        return NULL;
    }

    lws_log_info("[MEDIA_SESSION] Media session created for UAC\n");

    /* 开始收集ICE candidates (异步) */
    if (lws_sess_gather_candidates(dlg->sess) < 0) {
        lws_log_error(LWS_ERR_MEDIA, "Failed to start candidate gathering\n");
        lws_agent_destroy_dialog(agent, dlg);
        return NULL;
    }

    lws_log_info("[MEDIA_SESSION] Started gathering candidates (UAC)\n");

    /*
     * 注意：不在这里发送INVITE
     * INVITE将在sess_on_sdp_ready回调中发送（当SDP准备好后）
     */

    /* Return public dialog handle */
    return (lws_dialog_t*)dlg;
}

int lws_agent_answer_call(lws_agent_t* agent, lws_dialog_t* dialog)
{
    if (!agent || !dialog) {
        return LWS_EINVAL;
    }

    /* Cast to internal dialog */
    lws_dialog_intl_t* dlg = (lws_dialog_intl_t*)dialog;

    /* Verify we have a UAS transaction */
    if (!dlg->uas_txn) {
        lws_log_error(LWS_ERROR, "No UAS transaction for dialog\n");
        return LWS_ERROR;
    }

    /* Verify dialog state */
    if (dlg->state != LWS_DIALOG_STATE_INCOMING) {
        lws_log_error(LWS_ERROR, "Dialog not in INCOMING state (current: %d)\n", dlg->state);
        return LWS_ERROR;
    }

    /* Verify media session exists (should be created in sip_uas_oninvite) */
    if (!dlg->sess) {
        lws_log_error(LWS_ERROR, "No media session for dialog\n");
        return LWS_ERROR;
    }

    /*
     * 开始收集ICE candidates（异步操作）
     * 当SDP准备好后，sess_on_sdp_ready回调会被调用
     * 在回调中会发送200 OK with SDP
     */
    int ret = lws_sess_gather_candidates(dlg->sess);
    if (ret != 0) {
        lws_log_error(LWS_ERROR, "Failed to start gathering candidates\n");
        return LWS_ERROR;
    }

    lws_log_info("Started gathering ICE candidates for answer (UAS)\n");
    lws_log_info("[MEDIA_SESSION] Gathering candidates (will send 200 OK when ready)\n");

    /*
     * 注意：不在这里发送200 OK
     * 200 OK将在sess_on_sdp_ready回调中发送（当SDP准备好后）
     */

    return LWS_OK;
}

int lws_agent_reject_call(lws_agent_t* agent, lws_dialog_t* dialog,
                          int status_code, const char* reason_phrase)
{
    if (!agent || !dialog) {
        return LWS_EINVAL;
    }

    /* Cast to internal dialog */
    lws_dialog_intl_t* dlg = (lws_dialog_intl_t*)dialog;

    /* Verify we have a UAS transaction */
    if (!dlg->uas_txn) {
        lws_log_error(LWS_ERROR, "No UAS transaction for dialog\n");
        return LWS_ERROR;
    }

    /* Verify dialog state */
    if (dlg->state != LWS_DIALOG_STATE_INCOMING) {
        lws_log_error(LWS_ERROR, "Dialog not in INCOMING state (current: %d)\n", dlg->state);
        return LWS_ERROR;
    }

    /* Validate status code (should be 4xx, 5xx, or 6xx) */
    if (status_code < 400 || status_code >= 700) {
        lws_log_error(LWS_EINVAL, "Invalid status code for rejection: %d\n", status_code);
        return LWS_EINVAL;
    }

    /* Use default reason phrase if not provided */
    const char* reason = reason_phrase;
    if (!reason || reason[0] == '\0') {
        /* Set default reason based on status code */
        switch (status_code) {
            case 486: reason = "Busy Here"; break;
            case 603: reason = "Decline"; break;
            case 480: reason = "Temporarily Unavailable"; break;
            default:  reason = "Call Rejected"; break;
        }
    }

    /* Send error response */
    int ret = sip_uas_reply(dlg->uas_txn, status_code, NULL, 0, agent);
    if (ret < 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "Failed to send %d %s\n", status_code, reason);
        return LWS_ERR_SIP_SEND;
    }

    /* Update dialog state */
    lws_dialog_state_t old_state = dlg->state;
    dlg->state = LWS_DIALOG_STATE_FAILED;

    /* Trigger callback */
    if (agent->handler.on_dialog_state_changed) {
        agent->handler.on_dialog_state_changed(agent, dialog, old_state,
                                               LWS_DIALOG_STATE_FAILED,
                                               agent->handler.userdata);
    }

    /* Release the UAS transaction */
    sip_uas_transaction_release(dlg->uas_txn);
    dlg->uas_txn = NULL;

    /* Destroy dialog */
    lws_agent_destroy_dialog(agent, dlg);

    lws_log_info("Call rejected with %d %s\n", status_code, reason);

    return LWS_OK;
}

int lws_agent_hangup(lws_agent_t* agent, lws_dialog_t* dialog)
{
    if (!agent || !dialog) {
        return LWS_EINVAL;
    }

    /* Cast to internal dialog */
    lws_dialog_intl_t* dlg = (lws_dialog_intl_t*)dialog;

    /* Verify dialog has sip_dialog */
    if (!dlg->sip_dialog) {
        lws_log_error(LWS_ERROR, "No SIP dialog for hangup\n");
        return LWS_ERROR;
    }

    /* Verify dialog is in a state that can be hung up */
    if (dlg->state != LWS_DIALOG_STATE_CONFIRMED &&
        dlg->state != LWS_DIALOG_STATE_EARLY) {
        lws_log_error(LWS_ERROR, "Dialog not in valid state for hangup (current: %d)\n",
                     dlg->state);
        return LWS_ERROR;
    }

    /* Send BYE request */
    struct sip_uac_transaction_t* bye_txn = sip_uac_bye(
        agent->sip_agent,
        dlg->sip_dialog,
        uac_onreply_callback,  /* Use same callback as INVITE */
        agent
    );

    if (!bye_txn) {
        lws_log_error(LWS_ERR_SIP_SEND, "Failed to create BYE transaction\n");
        return LWS_ERR_SIP_SEND;
    }

    /* Send the BYE transaction */
    int ret = sip_uac_send(bye_txn, NULL, 0, &agent->sip_transport, agent);
    if (ret < 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "Failed to send BYE: %d\n", ret);
        sip_uac_transaction_release(bye_txn);
        return LWS_ERR_SIP_SEND;
    }

    /* Release transaction immediately (fire-and-forget) */
    sip_uac_transaction_release(bye_txn);

    /* Update dialog state */
    lws_dialog_state_t old_state = dlg->state;
    dlg->state = LWS_DIALOG_STATE_TERMINATED;

    /* Trigger callback */
    if (agent->handler.on_dialog_state_changed) {
        agent->handler.on_dialog_state_changed(agent, dialog, old_state,
                                               LWS_DIALOG_STATE_TERMINATED,
                                               agent->handler.userdata);
    }

    /* Stop media session if active */
    if (dlg->sess) {
        lws_sess_stop(dlg->sess);
    }

    /* Destroy dialog */
    lws_agent_destroy_dialog(agent, dlg);

    lws_log_info("Call hung up, sent BYE\n");
    lws_log_info("[MEDIA_SESSION] Media session terminated (UAC sent BYE)\n");

    return LWS_OK;
}

int lws_agent_cancel_call(lws_agent_t* agent, lws_dialog_t* dialog)
{
    if (!agent || !dialog) {
        return LWS_EINVAL;
    }

    /* Cast to internal dialog */
    lws_dialog_intl_t* dlg = (lws_dialog_intl_t*)dialog;

    /* Verify we have a UAC transaction */
    if (!dlg->invite_txn) {
        lws_log_error(LWS_ERROR, "No UAC transaction for dialog\n");
        return LWS_ERROR;
    }

    /* Verify dialog state - CANCEL only makes sense for outgoing calls that haven't completed */
    if (dlg->state != LWS_DIALOG_STATE_CALLING &&
        dlg->state != LWS_DIALOG_STATE_EARLY) {
        lws_log_error(LWS_ERROR, "Dialog not in valid state for CANCEL (current: %d)\n", dlg->state);
        return LWS_ERROR;
    }

    /* Send CANCEL request via libsip
     * Note: sip_uac_cancel sends CANCEL for the original INVITE transaction
     * The CANCEL will be sent with the same Call-ID, From, To, CSeq as INVITE
     */
    struct sip_uac_transaction_t* cancel_txn = sip_uac_cancel(agent->sip_agent, dlg->invite_txn, uac_onreply_callback, agent);
    if (!cancel_txn) {
        lws_log_error(LWS_ERR_SIP_SEND, "Failed to create CANCEL transaction\n");
        return LWS_ERR_SIP_SEND;
    }

    /* Send the CANCEL transaction */
    int ret = sip_uac_send(cancel_txn, NULL, 0, &agent->sip_transport, agent);
    if (ret < 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "Failed to send CANCEL: %d\n", ret);
        sip_uac_transaction_release(cancel_txn);
        return LWS_ERR_SIP_SEND;
    }

    /* Release transaction immediately (fire-and-forget) */
    sip_uac_transaction_release(cancel_txn);

    /* Update dialog state to TERMINATED
     * The UAC will receive a 487 Request Terminated response to the INVITE
     * and a 200 OK response to the CANCEL, but we consider the call terminated now
     */
    lws_dialog_state_t old_state = dlg->state;
    dlg->state = LWS_DIALOG_STATE_TERMINATED;

    /* Trigger callback */
    if (agent->handler.on_dialog_state_changed) {
        agent->handler.on_dialog_state_changed(agent, dialog, old_state,
                                               LWS_DIALOG_STATE_TERMINATED,
                                               agent->handler.userdata);
    }

    /* Release the UAC transaction
     * Note: The transaction will handle receiving 487 and CANCEL responses
     */
    sip_uac_transaction_release(dlg->invite_txn);
    dlg->invite_txn = NULL;

    /* Stop media session if it was started */
    if (dlg->sess) {
        lws_sess_stop(dlg->sess);
    }

    /* Destroy dialog */
    lws_agent_destroy_dialog(agent, dlg);

    lws_log_info("Call cancelled, sent CANCEL\n");

    return LWS_OK;
}

int lws_agent_get_dialogs(lws_agent_t* agent, lws_dialog_t** dialogs, int max_count)
{
    if (!agent || !dialogs || max_count <= 0) {
        return LWS_EINVAL;
    }

    /* Iterate through dialog list and copy pointers */
    int count = 0;
    lws_dialog_intl_t* dlg = agent->dialogs;

    while (dlg && count < max_count) {
        /* Point to the public dialog structure */
        dialogs[count] = &dlg->public;
        count++;
        dlg = dlg->next;
    }

    return count;
}

lws_agent_state_t lws_agent_get_state(lws_agent_t* agent)
{
    return agent ? agent->state : LWS_AGENT_STATE_IDLE;
}

/* ========================================
 * Dialog query APIs
 * ======================================== */

lws_dialog_state_t lws_dialog_get_state(lws_dialog_t* dialog)
{
    if (!dialog) {
        return LWS_DIALOG_STATE_NULL;
    }

    lws_dialog_intl_t* dlg = (lws_dialog_intl_t*)dialog;
    return dlg->state;
}

const char* lws_dialog_get_call_id(lws_dialog_t* dialog)
{
    if (!dialog) {
        return NULL;
    }

    return dialog->call_id;
}

int lws_dialog_get_remote_addr(lws_dialog_t* dialog, lws_sip_addr_t* addr)
{
    if (!dialog || !addr) {
        return LWS_EINVAL;
    }

    /* Parse the remote URI */
    return lws_sip_parse_uri(dialog->remote_uri, addr);
}

int lws_dialog_get_local_addr(lws_dialog_t* dialog, lws_sip_addr_t* addr)
{
    if (!dialog || !addr) {
        return LWS_EINVAL;
    }

    /* Parse the local URI */
    return lws_sip_parse_uri(dialog->local_uri, addr);
}

const char* lws_dialog_get_remote_sdp(lws_dialog_t* dialog)
{
    if (!dialog) {
        return NULL;
    }

    lws_dialog_intl_t* dlg = (lws_dialog_intl_t*)dialog;
    return dlg->remote_sdp[0] ? dlg->remote_sdp : NULL;
}

/* ========================================
 * Helper Functions Implementation
 * ======================================== */

/**
 * @brief Parse SIP URI into address components
 *
 * Supported formats:
 * - sip:user@domain
 * - sip:user@domain:port
 * - sip:nickname<user@domain>
 * - sip:nickname<user@domain:port>
 * - "nickname"<sip:user@domain>
 * - "nickname"<sip:user@domain:port>
 */
int lws_sip_parse_uri(const char* uri, lws_sip_addr_t* addr)
{
    if (!uri || !addr) {
        return LWS_EINVAL;
    }

    memset(addr, 0, sizeof(lws_sip_addr_t));

    const char* p = uri;

    /* Skip whitespace */
    while (*p && (*p == ' ' || *p == '\t')) {
        p++;
    }

    /* Check for display name (nickname) */
    if (*p == '"') {
        /* Quoted display name: "nickname"<sip:...> */
        p++; /* Skip opening quote */
        const char* name_end = strchr(p, '"');
        if (!name_end) {
            return LWS_EINVAL;
        }

        size_t name_len = name_end - p;
        if (name_len >= LWS_MAX_NICKNAME_LEN) {
            name_len = LWS_MAX_NICKNAME_LEN - 1;
        }
        strncpy(addr->nickname, p, name_len);
        addr->nickname[name_len] = '\0';

        p = name_end + 1;

        /* Skip whitespace and < */
        while (*p && (*p == ' ' || *p == '\t' || *p == '<')) {
            p++;
        }
    } else {
        /* Check for unquoted display name: nickname<sip:...> */
        const char* bracket = strchr(p, '<');
        if (bracket) {
            /* Extract display name */
            const char* name_start = p;
            const char* name_end = bracket - 1;

            /* Trim trailing whitespace */
            while (name_end > name_start && (*name_end == ' ' || *name_end == '\t')) {
                name_end--;
            }

            size_t name_len = name_end - name_start + 1;
            if (name_len > 0 && name_len < LWS_MAX_NICKNAME_LEN) {
                strncpy(addr->nickname, name_start, name_len);
                addr->nickname[name_len] = '\0';
            }

            p = bracket + 1; /* Move past '<' */
        }
    }

    /* Skip "sip:" or "sips:" prefix */
    if (strncmp(p, "sip:", 4) == 0) {
        p += 4;
    } else if (strncmp(p, "sips:", 5) == 0) {
        p += 5;
    }

    /* Find '@' separator */
    const char* at_sign = strchr(p, '@');
    if (!at_sign) {
        /* No '@' - might be just domain */
        return LWS_EINVAL;
    }

    /* Extract username */
    size_t user_len = at_sign - p;
    if (user_len >= LWS_MAX_USERNAME_LEN) {
        user_len = LWS_MAX_USERNAME_LEN - 1;
    }
    strncpy(addr->username, p, user_len);
    addr->username[user_len] = '\0';

    /* Move past '@' */
    p = at_sign + 1;

    /* Find end of domain (could be ':', '>', ';', or end of string) */
    const char* domain_end = p;
    while (*domain_end && *domain_end != ':' && *domain_end != '>' &&
           *domain_end != ';' && *domain_end != ' ') {
        domain_end++;
    }

    /* Extract domain */
    size_t domain_len = domain_end - p;
    if (domain_len >= LWS_MAX_DOMAIN_LEN) {
        domain_len = LWS_MAX_DOMAIN_LEN - 1;
    }
    strncpy(addr->domain, p, domain_len);
    addr->domain[domain_len] = '\0';

    /* Extract port if present */
    addr->port = 0;
    if (*domain_end == ':') {
        p = domain_end + 1;
        int port = 0;
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            p++;
        }
        if (port > 0 && port <= 65535) {
            addr->port = (uint16_t)port;
        }
    }

    return LWS_OK;
}

/**
 * @brief Convert SIP address to URI string
 *
 * Output format:
 * - If nickname exists: "nickname"<sip:username@domain:port>
 * - Otherwise: sip:username@domain:port
 * - Port is omitted if 0 or 5060
 */
int lws_sip_addr_to_string(const lws_sip_addr_t* addr, char* buf, size_t size)
{
    if (!addr || !buf || size == 0) {
        return LWS_EINVAL;
    }

    int written = 0;

    /* Add display name if present */
    if (addr->nickname[0] != '\0') {
        written = snprintf(buf, size, "\"%s\"<sip:%s@%s",
                          addr->nickname, addr->username, addr->domain);
    } else {
        written = snprintf(buf, size, "sip:%s@%s",
                          addr->username, addr->domain);
    }

    if (written < 0 || (size_t)written >= size) {
        return LWS_ERROR;
    }

    /* Add port if not default */
    if (addr->port != 0 && addr->port != 5060) {
        int port_len = snprintf(buf + written, size - written, ":%u", addr->port);
        if (port_len < 0 || (size_t)(written + port_len) >= size) {
            return LWS_ERROR;
        }
        written += port_len;
    }

    /* Close bracket if we added display name */
    if (addr->nickname[0] != '\0') {
        if ((size_t)(written + 1) >= size) {
            return LWS_ERROR;
        }
        buf[written++] = '>';
        buf[written] = '\0';
    }

    return written;
}

/**
 * @brief Get Agent state name
 */
const char* lws_agent_state_name(lws_agent_state_t state)
{
    switch (state) {
        case LWS_AGENT_STATE_IDLE:           return "IDLE";
        case LWS_AGENT_STATE_REGISTERING:    return "REGISTERING";
        case LWS_AGENT_STATE_REGISTERED:     return "REGISTERED";
        case LWS_AGENT_STATE_REGISTER_FAILED: return "REGISTER_FAILED";
        case LWS_AGENT_STATE_UNREGISTERING:  return "UNREGISTERING";
        case LWS_AGENT_STATE_UNREGISTERED:   return "UNREGISTERED";
        default:                             return "UNKNOWN";
    }
}

/**
 * @brief Get Dialog state name
 */
const char* lws_dialog_state_name(lws_dialog_state_t state)
{
    switch (state) {
        case LWS_DIALOG_STATE_NULL:       return "NULL";
        case LWS_DIALOG_STATE_CALLING:    return "CALLING";
        case LWS_DIALOG_STATE_INCOMING:   return "INCOMING";
        case LWS_DIALOG_STATE_EARLY:      return "EARLY";
        case LWS_DIALOG_STATE_CONFIRMED:  return "CONFIRMED";
        case LWS_DIALOG_STATE_TERMINATED: return "TERMINATED";
        case LWS_DIALOG_STATE_FAILED:     return "FAILED";
        default:                          return "UNKNOWN";
    }
}

/**
 * @brief Initialize default Agent configuration
 */
void lws_agent_init_default_config(
    lws_agent_config_t* config,
    const char* username,
    const char* password,
    const char* domain,
    lws_trans_t* trans)
{
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(lws_agent_config_t));

    /* Set account credentials */
    if (username) {
        strncpy(config->username, username, LWS_MAX_USERNAME_LEN - 1);
    }
    if (password) {
        strncpy(config->password, password, LWS_MAX_PASSWORD_LEN - 1);
    }
    if (domain) {
        strncpy(config->domain, domain, LWS_MAX_DOMAIN_LEN - 1);
        /* Use domain as default registrar */
        strncpy(config->registrar, domain, LWS_MAX_HOSTNAME_LEN - 1);
    }

    /* Default values */
    config->registrar_port = 5060;
    config->auto_register = 1;
    config->register_expires = 3600;

    /* Default User-Agent */
    snprintf(config->user_agent, LWS_MAX_USER_AGENT_LEN,
             "lwsip/%s", LWS_VERSION_STRING);

    /* Note: trans parameter is not used in current implementation,
     * as transport is created internally by lws_agent_create() */
    (void)trans;
}

/**
 * @brief Get Agent's SIP URI
 */
int lws_agent_get_uri(lws_agent_t* agent, char* buf, size_t size)
{
    if (!agent || !buf || size == 0) {
        return LWS_EINVAL;
    }

    lws_sip_addr_t addr;
    memset(&addr, 0, sizeof(addr));

    /* Build address from agent config */
    strncpy(addr.username, agent->config.username, LWS_MAX_USERNAME_LEN - 1);
    strncpy(addr.domain, agent->config.domain, LWS_MAX_DOMAIN_LEN - 1);

    if (agent->config.nickname[0] != '\0') {
        strncpy(addr.nickname, agent->config.nickname, LWS_MAX_NICKNAME_LEN - 1);
    }

    /* Use helper function to format URI */
    return lws_sip_addr_to_string(&addr, buf, size);
}
