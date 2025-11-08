/**
 * @file trans_stub.c
 * @brief Intelligent transport stub implementation
 *
 * Provides scenario-based SIP response generation for unit testing.
 */

#include "trans_stub.h"
#include "lws_trans.h"
#include "lws_mem.h"
#include "lws_log.h"
#include "list.h"
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

/* ========================================
 * Response Queue Node
 * ======================================== */

typedef struct response_node_t {
    char* data;                    /**< Response data (allocated) */
    int len;                       /**< Data length */
    lws_addr_t from;              /**< Source address */
    uint64_t deliver_time_ms;     /**< When to deliver (milliseconds) */
    struct list_head list;        /**< List linkage */
} response_node_t;

/* ========================================
 * Stub State
 * ======================================== */

typedef struct {
    trans_stub_scenario_t scenario;           /**< Current test scenario */
    int response_delay_ms;                    /**< Response delay */

    /* Last request tracking */
    char last_request[4096];                  /**< Last sent SIP request */
    int last_request_len;                     /**< Length of last request */

    /* Transport handler (set by lws_trans_create) */
    lws_trans_handler_t handler;              /**< Handler callbacks */
    void* trans_instance;                     /**< Transport instance */

    /* Response queue */
    struct list_head response_queue;          /**< Pending responses */

    int initialized;                          /**< Init flag */
} trans_stub_state_t;

static trans_stub_state_t g_stub_state = {0};

/* ========================================
 * Helper Functions
 * ======================================== */

/**
 * @brief Get current time in milliseconds
 */
static uint64_t get_current_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

/**
 * @brief Parse SIP method from request
 * @param data SIP request data
 * @param len Data length
 * @return Method string (points into data buffer), or NULL if invalid
 */
static const char* parse_sip_method(const char* data, int len)
{
    static char method[16];

    if (!data || len < 4) {
        return NULL;
    }

    /* SIP request format: METHOD sip:... SIP/2.0 */
    const char* space = memchr(data, ' ', len);
    if (!space || space == data) {
        return NULL;
    }

    int method_len = space - data;
    if (method_len >= (int)sizeof(method)) {
        method_len = sizeof(method) - 1;
    }

    memcpy(method, data, method_len);
    method[method_len] = '\0';

    return method;
}

/**
 * @brief Extract Call-ID from SIP message
 * @param data SIP message
 * @param len Message length
 * @param call_id Output buffer for Call-ID
 * @param call_id_size Buffer size
 * @return 0 on success, -1 if not found
 */
static int extract_call_id(const char* data, int len, char* call_id, size_t call_id_size)
{
    const char* call_id_hdr = strstr(data, "Call-ID: ");
    if (!call_id_hdr) {
        call_id_hdr = strstr(data, "i: ");  /* Compact form */
        if (!call_id_hdr) {
            return -1;
        }
        call_id_hdr += 3;
    } else {
        call_id_hdr += 9;
    }

    const char* end = memchr(call_id_hdr, '\r', len - (call_id_hdr - data));
    if (!end) {
        return -1;
    }

    size_t id_len = end - call_id_hdr;
    if (id_len >= call_id_size) {
        id_len = call_id_size - 1;
    }

    memcpy(call_id, call_id_hdr, id_len);
    call_id[id_len] = '\0';

    return 0;
}

/**
 * @brief Extract CSeq from SIP message
 * @param data SIP message
 * @param len Message length
 * @param cseq Output buffer for CSeq
 * @param cseq_size Buffer size
 * @return 0 on success, -1 if not found
 */
static int extract_cseq(const char* data, int len, char* cseq, size_t cseq_size)
{
    const char* cseq_hdr = strstr(data, "CSeq: ");
    if (!cseq_hdr) {
        return -1;
    }
    cseq_hdr += 6;

    const char* end = memchr(cseq_hdr, '\r', len - (cseq_hdr - data));
    if (!end) {
        return -1;
    }

    size_t cseq_len = end - cseq_hdr;
    if (cseq_len >= cseq_size) {
        cseq_len = cseq_size - 1;
    }

    memcpy(cseq, cseq_hdr, cseq_len);
    cseq[cseq_len] = '\0';

    return 0;
}

/**
 * @brief Extract Via from SIP message
 * @param data SIP message
 * @param len Message length
 * @param via Output buffer for Via
 * @param via_size Buffer size
 * @return 0 on success, -1 if not found
 */
static int extract_via(const char* data, int len, char* via, size_t via_size)
{
    const char* via_hdr = strstr(data, "Via: ");
    if (!via_hdr) {
        via_hdr = strstr(data, "v: ");  /* Compact form */
        if (!via_hdr) {
            return -1;
        }
        via_hdr += 3;
    } else {
        via_hdr += 5;
    }

    const char* end = memchr(via_hdr, '\r', len - (via_hdr - data));
    if (!end) {
        return -1;
    }

    size_t via_len = end - via_hdr;
    if (via_len >= via_size) {
        via_len = via_size - 1;
    }

    memcpy(via, via_hdr, via_len);
    via[via_len] = '\0';

    return 0;
}

/**
 * @brief Extract From from SIP message
 * @param data SIP message
 * @param len Message length
 * @param from Output buffer for From
 * @param from_size Buffer size
 * @return 0 on success, -1 if not found
 */
static int extract_from(const char* data, int len, char* from, size_t from_size)
{
    const char* from_hdr = strstr(data, "From: ");
    if (!from_hdr) {
        from_hdr = strstr(data, "f: ");  /* Compact form */
        if (!from_hdr) {
            return -1;
        }
        from_hdr += 3;
    } else {
        from_hdr += 6;
    }

    const char* end = memchr(from_hdr, '\r', len - (from_hdr - data));
    if (!end) {
        return -1;
    }

    size_t from_len = end - from_hdr;
    if (from_len >= from_size) {
        from_len = from_size - 1;
    }

    memcpy(from, from_hdr, from_len);
    from[from_len] = '\0';

    return 0;
}

/**
 * @brief Extract To from SIP message
 * @param data SIP message
 * @param len Message length
 * @param to Output buffer for To
 * @param to_size Buffer size
 * @return 0 on success, -1 if not found
 */
static int extract_to(const char* data, int len, char* to, size_t to_size)
{
    const char* to_hdr = strstr(data, "To: ");
    if (!to_hdr) {
        to_hdr = strstr(data, "t: ");  /* Compact form */
        if (!to_hdr) {
            return -1;
        }
        to_hdr += 3;
    } else {
        to_hdr += 4;
    }

    const char* end = memchr(to_hdr, '\r', len - (to_hdr - data));
    if (!end) {
        return -1;
    }

    size_t to_len = end - to_hdr;
    if (to_len >= to_size) {
        to_len = to_size - 1;
    }

    memcpy(to, to_hdr, to_len);
    to[to_len] = '\0';

    return 0;
}

/**
 * @brief Queue a SIP response for async delivery
 * @param response Response data
 * @param len Response length
 * @param from Source address
 * @return 0 on success, -1 on error
 */
static int queue_response(const char* response, int len, const lws_addr_t* from)
{
    response_node_t* node = (response_node_t*)lws_calloc(1, sizeof(response_node_t));
    if (!node) {
        return -1;
    }

    node->data = (char*)lws_malloc(len);
    if (!node->data) {
        lws_free(node);
        return -1;
    }

    memcpy(node->data, response, len);
    node->len = len;
    if (from) {
        node->from = *from;
    }
    node->deliver_time_ms = get_current_time_ms() + g_stub_state.response_delay_ms;

    LIST_INIT_HEAD(&node->list);
    list_insert_before(&node->list, &g_stub_state.response_queue);

    return 0;
}

/* ========================================
 * SIP Response Generators
 * ======================================== */

/**
 * @brief Generate 200 OK response for REGISTER
 */
static int generate_register_200_ok(const char* request, int req_len,
                                    char* response, size_t resp_size)
{
    char call_id[128];
    char cseq[64];
    char via[256];
    char from[256];
    char to[256];

    if (extract_call_id(request, req_len, call_id, sizeof(call_id)) != 0) {
        strcpy(call_id, "stub-call-id");
    }

    if (extract_cseq(request, req_len, cseq, sizeof(cseq)) != 0) {
        strcpy(cseq, "1 REGISTER");
    }

    if (extract_via(request, req_len, via, sizeof(via)) != 0) {
        strcpy(via, "SIP/2.0/UDP 127.0.0.1:5060;branch=stub-branch");
    }

    if (extract_from(request, req_len, from, sizeof(from)) != 0) {
        strcpy(from, "<sip:test@stub.com>;tag=stub-from-tag");
    }

    if (extract_to(request, req_len, to, sizeof(to)) != 0) {
        strcpy(to, "<sip:test@stub.com>");
    }

    /* Add tag to To header if not present */
    char to_with_tag[300];
    if (strstr(to, "tag=") == NULL) {
        snprintf(to_with_tag, sizeof(to_with_tag), "%s;tag=stub-to-tag", to);
    } else {
        strncpy(to_with_tag, to, sizeof(to_with_tag) - 1);
        to_with_tag[sizeof(to_with_tag) - 1] = '\0';
    }

    return snprintf(response, resp_size,
        "SIP/2.0 200 OK\r\n"
        "Via: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %s\r\n"
        "Contact: <sip:test@127.0.0.1:5060>\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        via, from, to_with_tag, call_id, cseq);
}

/**
 * @brief Generate 401 Unauthorized response for REGISTER
 */
static int generate_register_401_unauth(const char* request, int req_len,
                                        char* response, size_t resp_size)
{
    char call_id[128], cseq[64], via[256], from[256], to[256];

    extract_call_id(request, req_len, call_id, sizeof(call_id));
    extract_cseq(request, req_len, cseq, sizeof(cseq));
    extract_via(request, req_len, via, sizeof(via));
    extract_from(request, req_len, from, sizeof(from));
    extract_to(request, req_len, to, sizeof(to));

    char to_with_tag[300];
    if (strstr(to, "tag=") == NULL) {
        snprintf(to_with_tag, sizeof(to_with_tag), "%s;tag=stub-to-tag", to);
    } else {
        strncpy(to_with_tag, to, sizeof(to_with_tag) - 1);
        to_with_tag[sizeof(to_with_tag) - 1] = '\0';
    }

    return snprintf(response, resp_size,
        "SIP/2.0 401 Unauthorized\r\n"
        "Via: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %s\r\n"
        "WWW-Authenticate: Digest realm=\"stub.com\", nonce=\"stub-nonce-12345\"\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        via, from, to_with_tag, call_id, cseq);
}

/**
 * @brief Generate 403 Forbidden response for REGISTER
 */
static int generate_register_403_forbidden(const char* request, int req_len,
                                          char* response, size_t resp_size)
{
    char call_id[128], cseq[64], via[256], from[256], to[256];

    extract_call_id(request, req_len, call_id, sizeof(call_id));
    extract_cseq(request, req_len, cseq, sizeof(cseq));
    extract_via(request, req_len, via, sizeof(via));
    extract_from(request, req_len, from, sizeof(from));
    extract_to(request, req_len, to, sizeof(to));

    char to_with_tag[300];
    if (strstr(to, "tag=") == NULL) {
        snprintf(to_with_tag, sizeof(to_with_tag), "%s;tag=stub-to-tag", to);
    } else {
        strncpy(to_with_tag, to, sizeof(to_with_tag) - 1);
        to_with_tag[sizeof(to_with_tag) - 1] = '\0';
    }

    return snprintf(response, resp_size,
        "SIP/2.0 403 Forbidden\r\n"
        "Via: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %s\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        via, from, to_with_tag, call_id, cseq);
}

/**
 * @brief Generate 180 Ringing response for INVITE
 */
static int generate_invite_180_ringing(const char* request, int req_len,
                                       char* response, size_t resp_size)
{
    char call_id[128], cseq[64], via[256], from[256], to[256];

    extract_call_id(request, req_len, call_id, sizeof(call_id));
    extract_cseq(request, req_len, cseq, sizeof(cseq));
    extract_via(request, req_len, via, sizeof(via));
    extract_from(request, req_len, from, sizeof(from));
    extract_to(request, req_len, to, sizeof(to));

    char to_with_tag[300];
    if (strstr(to, "tag=") == NULL) {
        snprintf(to_with_tag, sizeof(to_with_tag), "%s;tag=stub-to-tag", to);
    } else {
        strncpy(to_with_tag, to, sizeof(to_with_tag) - 1);
        to_with_tag[sizeof(to_with_tag) - 1] = '\0';
    }

    return snprintf(response, resp_size,
        "SIP/2.0 180 Ringing\r\n"
        "Via: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %s\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        via, from, to_with_tag, call_id, cseq);
}

/**
 * @brief Generate 200 OK response for INVITE
 */
static int generate_invite_200_ok(const char* request, int req_len,
                                  char* response, size_t resp_size)
{
    char call_id[128], cseq[64], via[256], from[256], to[256];

    extract_call_id(request, req_len, call_id, sizeof(call_id));
    extract_cseq(request, req_len, cseq, sizeof(cseq));
    extract_via(request, req_len, via, sizeof(via));
    extract_from(request, req_len, from, sizeof(from));
    extract_to(request, req_len, to, sizeof(to));

    char to_with_tag[300];
    if (strstr(to, "tag=") == NULL) {
        snprintf(to_with_tag, sizeof(to_with_tag), "%s;tag=stub-to-tag", to);
    } else {
        strncpy(to_with_tag, to, sizeof(to_with_tag) - 1);
        to_with_tag[sizeof(to_with_tag) - 1] = '\0';
    }

    const char* sdp =
        "v=0\r\n"
        "o=stub 0 0 IN IP4 127.0.0.1\r\n"
        "s=lwsip stub\r\n"
        "c=IN IP4 127.0.0.1\r\n"
        "t=0 0\r\n"
        "m=audio 9000 RTP/AVP 0 8\r\n"
        "a=rtpmap:0 PCMU/8000\r\n"
        "a=rtpmap:8 PCMA/8000\r\n";

    return snprintf(response, resp_size,
        "SIP/2.0 200 OK\r\n"
        "Via: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %s\r\n"
        "Contact: <sip:callee@127.0.0.1:5060>\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "%s",
        via, from, to_with_tag, call_id, cseq, (int)strlen(sdp), sdp);
}

/**
 * @brief Generate 486 Busy Here response for INVITE
 */
static int generate_invite_486_busy(const char* request, int req_len,
                                    char* response, size_t resp_size)
{
    char call_id[128], cseq[64], via[256], from[256], to[256];

    extract_call_id(request, req_len, call_id, sizeof(call_id));
    extract_cseq(request, req_len, cseq, sizeof(cseq));
    extract_via(request, req_len, via, sizeof(via));
    extract_from(request, req_len, from, sizeof(from));
    extract_to(request, req_len, to, sizeof(to));

    char to_with_tag[300];
    if (strstr(to, "tag=") == NULL) {
        snprintf(to_with_tag, sizeof(to_with_tag), "%s;tag=stub-to-tag", to);
    } else {
        strncpy(to_with_tag, to, sizeof(to_with_tag) - 1);
        to_with_tag[sizeof(to_with_tag) - 1] = '\0';
    }

    return snprintf(response, resp_size,
        "SIP/2.0 486 Busy Here\r\n"
        "Via: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %s\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        via, from, to_with_tag, call_id, cseq);
}

/**
 * @brief Generate 603 Decline response for INVITE
 */
static int generate_invite_603_decline(const char* request, int req_len,
                                       char* response, size_t resp_size)
{
    char call_id[128], cseq[64], via[256], from[256], to[256];

    extract_call_id(request, req_len, call_id, sizeof(call_id));
    extract_cseq(request, req_len, cseq, sizeof(cseq));
    extract_via(request, req_len, via, sizeof(via));
    extract_from(request, req_len, from, sizeof(from));
    extract_to(request, req_len, to, sizeof(to));

    char to_with_tag[300];
    if (strstr(to, "tag=") == NULL) {
        snprintf(to_with_tag, sizeof(to_with_tag), "%s;tag=stub-to-tag", to);
    } else {
        strncpy(to_with_tag, to, sizeof(to_with_tag) - 1);
        to_with_tag[sizeof(to_with_tag) - 1] = '\0';
    }

    return snprintf(response, resp_size,
        "SIP/2.0 603 Decline\r\n"
        "Via: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %s\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        via, from, to_with_tag, call_id, cseq);
}

/**
 * @brief Generate 200 OK response for BYE
 */
static int generate_bye_200_ok(const char* request, int req_len,
                               char* response, size_t resp_size)
{
    char call_id[128], cseq[64], via[256], from[256], to[256];

    extract_call_id(request, req_len, call_id, sizeof(call_id));
    extract_cseq(request, req_len, cseq, sizeof(cseq));
    extract_via(request, req_len, via, sizeof(via));
    extract_from(request, req_len, from, sizeof(from));
    extract_to(request, req_len, to, sizeof(to));

    return snprintf(response, resp_size,
        "SIP/2.0 200 OK\r\n"
        "Via: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %s\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        via, from, to, call_id, cseq);
}

/**
 * @brief Generate 200 OK response for CANCEL
 */
static int generate_cancel_200_ok(const char* request, int req_len,
                                  char* response, size_t resp_size)
{
    char call_id[128], cseq[64], via[256], from[256], to[256];

    extract_call_id(request, req_len, call_id, sizeof(call_id));
    extract_cseq(request, req_len, cseq, sizeof(cseq));
    extract_via(request, req_len, via, sizeof(via));
    extract_from(request, req_len, from, sizeof(from));
    extract_to(request, req_len, to, sizeof(to));

    return snprintf(response, resp_size,
        "SIP/2.0 200 OK\r\n"
        "Via: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %s\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        via, from, to, call_id, cseq);
}

/* ========================================
 * Request Handler
 * ======================================== */

/**
 * @brief Handle outgoing SIP request and generate responses based on scenario
 * @param data Request data
 * @param len Request length
 * @param dest Destination address
 * @return 0 on success, -1 on error
 */
static int handle_sip_request(const char* data, int len, const lws_addr_t* dest)
{
    char response[4096];
    int resp_len = 0;

    const char* method = parse_sip_method(data, len);
    if (!method) {
        lws_log_error(0, "Failed to parse SIP method\n");
        return -1;
    }

    /* Always print for debugging */
    printf("[TRANS_STUB] Received %s request (len=%d), scenario=%d\n", method, len, g_stub_state.scenario);
    lws_log_debug("Stub received %s request, scenario=%d\n", method, g_stub_state.scenario);

    /* Generate response based on scenario */
    if (strcmp(method, "REGISTER") == 0) {
        switch (g_stub_state.scenario) {
        case TRANS_STUB_SCENARIO_REGISTER_SUCCESS:
            resp_len = generate_register_200_ok(data, len, response, sizeof(response));
            break;

        case TRANS_STUB_SCENARIO_REGISTER_AUTH:
            /* Check if this is the first REGISTER or retry with Authorization */
            if (strstr(data, "Authorization:") == NULL) {
                /* First attempt - send 401 */
                resp_len = generate_register_401_unauth(data, len, response, sizeof(response));
            } else {
                /* Retry with auth - send 200 OK */
                resp_len = generate_register_200_ok(data, len, response, sizeof(response));
            }
            break;

        case TRANS_STUB_SCENARIO_REGISTER_FAILURE:
            resp_len = generate_register_403_forbidden(data, len, response, sizeof(response));
            break;

        default:
            return 0; /* No response for SCENARIO_NONE */
        }
    } else if (strcmp(method, "INVITE") == 0) {
        switch (g_stub_state.scenario) {
        case TRANS_STUB_SCENARIO_INVITE_SUCCESS:
            /* Send 180 Ringing first, then 200 OK */
            resp_len = generate_invite_180_ringing(data, len, response, sizeof(response));
            if (resp_len > 0) {
                queue_response(response, resp_len, dest);
            }
            /* Then 200 OK */
            resp_len = generate_invite_200_ok(data, len, response, sizeof(response));
            break;

        case TRANS_STUB_SCENARIO_INVITE_BUSY:
            resp_len = generate_invite_486_busy(data, len, response, sizeof(response));
            break;

        case TRANS_STUB_SCENARIO_INVITE_DECLINED:
            resp_len = generate_invite_603_decline(data, len, response, sizeof(response));
            break;

        default:
            return 0;
        }
    } else if (strcmp(method, "BYE") == 0) {
        switch (g_stub_state.scenario) {
        case TRANS_STUB_SCENARIO_BYE_SUCCESS:
            resp_len = generate_bye_200_ok(data, len, response, sizeof(response));
            break;

        default:
            return 0;
        }
    } else if (strcmp(method, "CANCEL") == 0) {
        switch (g_stub_state.scenario) {
        case TRANS_STUB_SCENARIO_CANCEL_SUCCESS:
            resp_len = generate_cancel_200_ok(data, len, response, sizeof(response));
            break;

        default:
            return 0;
        }
    } else {
        lws_log_warn(0, "Unhandled SIP method: %s\n", method);
        return 0;
    }

    /* Queue the response */
    if (resp_len > 0) {
        return queue_response(response, resp_len, dest);
    }

    return 0;
}

/* ========================================
 * Public API Implementation
 * ======================================== */

int trans_stub_init(void)
{
    if (g_stub_state.initialized) {
        return 0;
    }

    memset(&g_stub_state, 0, sizeof(g_stub_state));
    LIST_INIT_HEAD(&g_stub_state.response_queue);
    g_stub_state.initialized = 1;

    lws_log_info("Transport stub initialized\n");
    return 0;
}

void trans_stub_cleanup(void)
{
    if (!g_stub_state.initialized) {
        return;
    }

    /* Free all queued responses */
    struct list_head *pos, *n;
    response_node_t* node;

    list_for_each_safe(pos, n, &g_stub_state.response_queue) {
        node = list_entry(pos, response_node_t, list);
        list_remove(&node->list);
        lws_free(node->data);
        lws_free(node);
    }

    g_stub_state.initialized = 0;
    lws_log_info("Transport stub cleaned up\n");
}

void trans_stub_set_scenario(trans_stub_scenario_t scenario)
{
    g_stub_state.scenario = scenario;
    lws_log_debug("Stub scenario set to %d\n", scenario);
}

trans_stub_scenario_t trans_stub_get_scenario(void)
{
    return g_stub_state.scenario;
}

int trans_stub_process_responses(void)
{
    if (!g_stub_state.initialized) {
        return 0;
    }

    uint64_t now = get_current_time_ms();
    int processed = 0;

    struct list_head *pos, *n;
    response_node_t* node;

    list_for_each_safe(pos, n, &g_stub_state.response_queue) {
        node = list_entry(pos, response_node_t, list);

        if (node->deliver_time_ms <= now) {
            /* Time to deliver this response */
            if (g_stub_state.handler.on_data && g_stub_state.trans_instance) {
                g_stub_state.handler.on_data(
                    (lws_trans_t*)g_stub_state.trans_instance,
                    node->data,
                    node->len,
                    &node->from,
                    g_stub_state.handler.userdata
                );
            }

            /* Remove from queue */
            list_remove(&node->list);
            lws_free(node->data);
            lws_free(node);

            processed++;
        }
    }

    return processed;
}

const char* trans_stub_get_last_request(void)
{
    if (g_stub_state.last_request_len == 0) {
        return NULL;
    }
    return g_stub_state.last_request;
}

int trans_stub_get_last_request_len(void)
{
    return g_stub_state.last_request_len;
}

void trans_stub_clear_last_request(void)
{
    g_stub_state.last_request_len = 0;
    g_stub_state.last_request[0] = '\0';
}

void trans_stub_set_response_delay(int delay_ms)
{
    g_stub_state.response_delay_ms = delay_ms;
    lws_log_debug("Stub response delay set to %dms\n", delay_ms);
}

int trans_stub_get_response_delay(void)
{
    return g_stub_state.response_delay_ms;
}

/* ========================================
 * Internal API (called by lwsip_agent_stub.c)
 * ======================================== */

/**
 * @brief Store transport handler (called from lws_trans_create stub)
 */
void trans_stub_set_handler(const lws_trans_handler_t* handler, void* trans)
{
    if (handler) {
        g_stub_state.handler = *handler;
    }
    g_stub_state.trans_instance = trans;
}

/**
 * @brief Handle lws_trans_send call (called from lws_trans_send stub)
 */
int trans_stub_handle_send(const void* data, int len, const lws_addr_t* dest)
{
    /* Save last request */
    if (len > 0 && len < (int)sizeof(g_stub_state.last_request)) {
        memcpy(g_stub_state.last_request, data, len);
        g_stub_state.last_request_len = len;
        g_stub_state.last_request[len] = '\0';
    }

    /* Handle the request and generate response */
    handle_sip_request((const char*)data, len, dest);

    return len;
}
