/**
 * @file sip_fake.c
 * @brief Simplified SIP server for testing (no libsip dependency)
 *
 * This is a minimal SIP server that handles:
 * - REGISTER: Simple registration table management
 * - INVITE: Route from caller to callee
 * - ACK: Forward acknowledgments
 * - BYE: Terminate calls
 *
 * Uses simple string parsing instead of full SIP stack.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

/* ========================================
 * Constants
 * ======================================== */

#define SIP_PORT 5060
#define MAX_PACKET_SIZE 4096
#define MAX_REGISTRATIONS 100
#define MAX_USERNAME_LEN 64
#define MAX_ADDR_LEN 128

/* ========================================
 * Registration Table
 * ======================================== */

typedef struct {
    char username[MAX_USERNAME_LEN];    /**< Username (e.g., "1000") */
    char ip[INET_ADDRSTRLEN];           /**< IP address */
    uint16_t port;                      /**< Port number */
    int active;                         /**< 1 if active, 0 if expired */
} registration_t;

static registration_t g_registrations[MAX_REGISTRATIONS];

/* ========================================
 * Helper Functions - String Parsing
 * ======================================== */

/**
 * @brief Extract SIP method from request line
 * @return Pointer to method string (static buffer), or NULL if not found
 */
static const char* extract_method(const char* sip_msg)
{
    static char method[32];

    const char* end = strchr(sip_msg, ' ');
    if (!end) {
        return NULL;
    }

    size_t len = end - sip_msg;
    if (len >= sizeof(method)) {
        return NULL;
    }

    memcpy(method, sip_msg, len);
    method[len] = '\0';

    return method;
}

/**
 * @brief Extract Call-ID from SIP message
 */
static const char* extract_call_id(const char* sip_msg)
{
    static char call_id[256];

    const char* p = strstr(sip_msg, "Call-ID:");
    if (!p) {
        p = strstr(sip_msg, "i:");  // Short form
    }

    if (!p) {
        return NULL;
    }

    // Skip header name and whitespace
    p = strchr(p, ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;

    // Extract until newline
    const char* end = strchr(p, '\r');
    if (!end) {
        end = strchr(p, '\n');
    }
    if (!end) {
        return NULL;
    }

    size_t len = end - p;
    if (len >= sizeof(call_id)) {
        len = sizeof(call_id) - 1;
    }

    memcpy(call_id, p, len);
    call_id[len] = '\0';

    return call_id;
}

/**
 * @brief Extract CSeq from SIP message
 */
static const char* extract_cseq(const char* sip_msg)
{
    static char cseq[256];

    const char* p = strstr(sip_msg, "CSeq:");
    if (!p) {
        return NULL;
    }

    // Skip header name and whitespace
    p = strchr(p, ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;

    // Extract until newline
    const char* end = strchr(p, '\r');
    if (!end) {
        end = strchr(p, '\n');
    }
    if (!end) {
        return NULL;
    }

    size_t len = end - p;
    if (len >= sizeof(cseq)) {
        len = sizeof(cseq) - 1;
    }

    memcpy(cseq, p, len);
    cseq[len] = '\0';

    return cseq;
}

/**
 * @brief Extract username from SIP URI (e.g., "sip:1000@192.168.1.1" -> "1000")
 */
static int extract_username(const char* uri, char* username, size_t max_len)
{
    const char* start = strstr(uri, "sip:");
    if (!start) {
        start = uri;
    } else {
        start += 4;  // Skip "sip:"
    }

    const char* at = strchr(start, '@');
    if (!at) {
        return -1;
    }

    size_t len = at - start;
    if (len >= max_len) {
        return -1;
    }

    memcpy(username, start, len);
    username[len] = '\0';

    return 0;
}

/**
 * @brief Extract From header URI
 */
static const char* extract_from(const char* sip_msg)
{
    static char from[256];

    const char* p = strstr(sip_msg, "From:");
    if (!p) {
        p = strstr(sip_msg, "f:");  // Short form
    }

    if (!p) {
        return NULL;
    }

    // Find <sip:...>
    const char* uri_start = strchr(p, '<');
    if (!uri_start) {
        // No angle brackets, extract from : to ;
        p = strchr(p, ':');
        if (!p) return NULL;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        uri_start = p;

        const char* uri_end = strchr(uri_start, ';');
        if (!uri_end) {
            uri_end = strchr(uri_start, '\r');
        }
        if (!uri_end) {
            uri_end = strchr(uri_start, '\n');
        }
        if (!uri_end) {
            return NULL;
        }

        size_t len = uri_end - uri_start;
        if (len >= sizeof(from)) {
            len = sizeof(from) - 1;
        }
        memcpy(from, uri_start, len);
        from[len] = '\0';
        return from;
    }

    uri_start++;
    const char* uri_end = strchr(uri_start, '>');
    if (!uri_end) {
        return NULL;
    }

    size_t len = uri_end - uri_start;
    if (len >= sizeof(from)) {
        len = sizeof(from) - 1;
    }

    memcpy(from, uri_start, len);
    from[len] = '\0';

    return from;
}

/**
 * @brief Extract To header URI
 */
static const char* extract_to(const char* sip_msg)
{
    static char to[256];

    const char* p = strstr(sip_msg, "To:");
    if (!p) {
        p = strstr(sip_msg, "t:");  // Short form
    }

    if (!p) {
        return NULL;
    }

    // Find <sip:...>
    const char* uri_start = strchr(p, '<');
    if (!uri_start) {
        // No angle brackets, extract from : to ;
        p = strchr(p, ':');
        if (!p) return NULL;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        uri_start = p;

        const char* uri_end = strchr(uri_start, ';');
        if (!uri_end) {
            uri_end = strchr(uri_start, '\r');
        }
        if (!uri_end) {
            uri_end = strchr(uri_start, '\n');
        }
        if (!uri_end) {
            return NULL;
        }

        size_t len = uri_end - uri_start;
        if (len >= sizeof(to)) {
            len = sizeof(to) - 1;
        }
        memcpy(to, uri_start, len);
        to[len] = '\0';
        return to;
    }

    uri_start++;
    const char* uri_end = strchr(uri_start, '>');
    if (!uri_end) {
        return NULL;
    }

    size_t len = uri_end - uri_start;
    if (len >= sizeof(to)) {
        len = sizeof(to) - 1;
    }

    memcpy(to, uri_start, len);
    to[len] = '\0';

    return to;
}

/**
 * @brief Extract Via header
 */
static const char* extract_via(const char* sip_msg)
{
    static char via[512];

    const char* p = strstr(sip_msg, "Via:");
    if (!p) {
        p = strstr(sip_msg, "v:");  // Short form
    }

    if (!p) {
        return NULL;
    }

    // Skip header name and whitespace
    p = strchr(p, ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;

    // Extract until newline
    const char* end = strchr(p, '\r');
    if (!end) {
        end = strchr(p, '\n');
    }
    if (!end) {
        return NULL;
    }

    size_t len = end - p;
    if (len >= sizeof(via)) {
        len = sizeof(via) - 1;
    }

    memcpy(via, p, len);
    via[len] = '\0';

    return via;
}

/**
 * @brief Extract Contact header
 */
static const char* extract_contact(const char* sip_msg)
{
    static char contact[256];

    const char* p = strstr(sip_msg, "Contact:");
    if (!p) {
        p = strstr(sip_msg, "m:");  // Short form
    }

    if (!p) {
        return NULL;
    }

    // Find <sip:...>
    const char* uri_start = strchr(p, '<');
    if (!uri_start) {
        // No angle brackets, extract from : to end of line
        p = strchr(p, ':');
        if (!p) return NULL;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        uri_start = p;

        const char* uri_end = strchr(uri_start, '\r');
        if (!uri_end) {
            uri_end = strchr(uri_start, '\n');
        }
        if (!uri_end) {
            return NULL;
        }

        size_t len = uri_end - uri_start;
        if (len >= sizeof(contact)) {
            len = sizeof(contact) - 1;
        }
        memcpy(contact, uri_start, len);
        contact[len] = '\0';
        return contact;
    }

    uri_start++;
    const char* uri_end = strchr(uri_start, '>');
    if (!uri_end) {
        return NULL;
    }

    size_t len = uri_end - uri_start;
    if (len >= sizeof(contact)) {
        len = sizeof(contact) - 1;
    }

    memcpy(contact, uri_start, len);
    contact[len] = '\0';

    return contact;
}

/* ========================================
 * Registration Table Management
 * ======================================== */

/**
 * @brief Add or update registration
 */
static void add_registration(const char* username, const char* ip, uint16_t port)
{
    // Check if already registered
    for (int i = 0; i < MAX_REGISTRATIONS; i++) {
        if (g_registrations[i].active &&
            strcmp(g_registrations[i].username, username) == 0) {
            // Update existing registration
            strncpy(g_registrations[i].ip, ip, INET_ADDRSTRLEN - 1);
            g_registrations[i].port = port;
            printf("[SIP_FAKE] Updated registration: %s -> %s:%u\n",
                   username, ip, port);
            return;
        }
    }

    // Find empty slot
    for (int i = 0; i < MAX_REGISTRATIONS; i++) {
        if (!g_registrations[i].active) {
            strncpy(g_registrations[i].username, username, MAX_USERNAME_LEN - 1);
            strncpy(g_registrations[i].ip, ip, INET_ADDRSTRLEN - 1);
            g_registrations[i].port = port;
            g_registrations[i].active = 1;
            printf("[SIP_FAKE] New registration: %s -> %s:%u\n",
                   username, ip, port);
            return;
        }
    }

    printf("[SIP_FAKE] ERROR: Registration table full\n");
}

/**
 * @brief Find registration by username
 * @return Pointer to registration, or NULL if not found
 */
static registration_t* find_registration(const char* username)
{
    for (int i = 0; i < MAX_REGISTRATIONS; i++) {
        if (g_registrations[i].active &&
            strcmp(g_registrations[i].username, username) == 0) {
            return &g_registrations[i];
        }
    }
    return NULL;
}

/* ========================================
 * SIP Response Generation
 * ======================================== */

/**
 * @brief Generate SIP response
 */
static int generate_response(
    char* buf, size_t buf_size,
    int status_code, const char* reason_phrase,
    const char* via, const char* from, const char* to,
    const char* call_id, const char* cseq,
    const char* contact)
{
    int len = snprintf(buf, buf_size,
        "SIP/2.0 %d %s\r\n"
        "Via: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %s\r\n",
        status_code, reason_phrase,
        via, from, to, call_id, cseq);

    if (contact) {
        len += snprintf(buf + len, buf_size - len,
            "Contact: <sip:%s>\r\n", contact);
    }

    len += snprintf(buf + len, buf_size - len,
        "Content-Length: 0\r\n"
        "\r\n");

    return len;
}

/* ========================================
 * SIP Message Handlers
 * ======================================== */

/**
 * @brief Handle REGISTER request
 */
static void handle_register(
    int sock,
    const char* sip_msg,
    struct sockaddr_in* client_addr)
{
    printf("[SIP_FAKE] Handling REGISTER\n");
    fflush(stdout);

    // Extract headers
    printf("[SIP_FAKE]   Extracting headers...\n");
    fflush(stdout);

    const char* from = extract_from(sip_msg);
    printf("[SIP_FAKE]   From: %s\n", from ? from : "NULL");
    fflush(stdout);

    const char* to = extract_to(sip_msg);
    printf("[SIP_FAKE]   To: %s\n", to ? to : "NULL");
    fflush(stdout);

    const char* call_id = extract_call_id(sip_msg);
    printf("[SIP_FAKE]   Call-ID: %s\n", call_id ? call_id : "NULL");
    fflush(stdout);

    const char* cseq = extract_cseq(sip_msg);
    printf("[SIP_FAKE]   CSeq: %s\n", cseq ? cseq : "NULL");
    fflush(stdout);

    const char* via = extract_via(sip_msg);
    printf("[SIP_FAKE]   Via: %s\n", via ? via : "NULL");
    fflush(stdout);

    const char* contact = extract_contact(sip_msg);
    printf("[SIP_FAKE]   Contact: %s\n", contact ? contact : "NULL");
    fflush(stdout);

    if (!from || !to || !call_id || !cseq || !via) {
        printf("[SIP_FAKE]   ERROR: Missing required headers\n");
        fflush(stdout);
        return;
    }

    printf("[SIP_FAKE]   All required headers present\n");
    fflush(stdout);

    // Extract username from From header
    char username[MAX_USERNAME_LEN];
    if (extract_username(from, username, sizeof(username)) != 0) {
        printf("[SIP_FAKE]   ERROR: Failed to extract username from: %s\n", from);
        fflush(stdout);
        return;
    }

    printf("[SIP_FAKE]   Extracted username: %s\n", username);
    fflush(stdout);

    // Get client address
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr->sin_addr, client_ip, sizeof(client_ip));
    uint16_t client_port = ntohs(client_addr->sin_port);

    printf("[SIP_FAKE]   Client address: %s:%u\n", client_ip, client_port);
    fflush(stdout);

    // Add to registration table
    add_registration(username, client_ip, client_port);
    printf("[SIP_FAKE]   Added to registration table\n");
    fflush(stdout);

    // Generate 200 OK response
    char response[MAX_PACKET_SIZE];
    printf("[SIP_FAKE]   Generating 200 OK response...\n");
    fflush(stdout);

    int response_len = generate_response(
        response, sizeof(response),
        200, "OK",
        via, from, to, call_id, cseq,
        contact);

    printf("[SIP_FAKE]   Response generated, length=%d\n", response_len);
    fflush(stdout);

    // Send response
    printf("[SIP_FAKE]   Sending response to %s:%u...\n", client_ip, client_port);
    fflush(stdout);

    if (sendto(sock, response, response_len, 0,
               (struct sockaddr*)client_addr, sizeof(*client_addr)) < 0) {
        printf("[SIP_FAKE]   ERROR: Failed to send response: %s\n", strerror(errno));
        fflush(stdout);
        return;
    }

    printf("[SIP_FAKE]   Sent 200 OK for REGISTER (username=%s)\n", username);
    fflush(stdout);
}

/**
 * @brief Handle INVITE request
 */
static void handle_invite(
    int sock,
    const char* sip_msg,
    struct sockaddr_in* client_addr)
{
    printf("[SIP_FAKE] Handling INVITE\n");

    // Extract headers
    const char* from = extract_from(sip_msg);
    const char* to = extract_to(sip_msg);
    const char* call_id = extract_call_id(sip_msg);
    const char* cseq = extract_cseq(sip_msg);
    const char* via = extract_via(sip_msg);

    if (!from || !to || !call_id || !cseq || !via) {
        printf("[SIP_FAKE]   ERROR: Missing required headers\n");
        return;
    }

    // Extract target username from To header
    char target_username[MAX_USERNAME_LEN];
    if (extract_username(to, target_username, sizeof(target_username)) != 0) {
        printf("[SIP_FAKE]   ERROR: Failed to extract target username\n");
        return;
    }

    // Find target registration
    registration_t* target = find_registration(target_username);
    if (!target) {
        printf("[SIP_FAKE]   ERROR: Target %s not registered\n", target_username);

        // Send 404 Not Found
        char response[MAX_PACKET_SIZE];
        int response_len = generate_response(
            response, sizeof(response),
            404, "Not Found",
            via, from, to, call_id, cseq, NULL);

        sendto(sock, response, response_len, 0,
               (struct sockaddr*)client_addr, sizeof(*client_addr));
        return;
    }

    printf("[SIP_FAKE]   Routing INVITE to %s (%s:%u)\n",
           target_username, target->ip, target->port);

    // Forward INVITE to target
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(target->port);
    inet_pton(AF_INET, target->ip, &target_addr.sin_addr);

    if (sendto(sock, sip_msg, strlen(sip_msg), 0,
               (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
        printf("[SIP_FAKE]   ERROR: Failed to forward INVITE: %s\n", strerror(errno));
        return;
    }

    printf("[SIP_FAKE]   Forwarded INVITE to %s\n", target_username);
}

/**
 * @brief Handle ACK request
 */
static void handle_ack(
    int sock,
    const char* sip_msg,
    struct sockaddr_in* client_addr)
{
    printf("[SIP_FAKE] Handling ACK\n");

    // Extract To header to find target
    const char* to = extract_to(sip_msg);
    if (!to) {
        printf("[SIP_FAKE]   ERROR: Missing To header\n");
        return;
    }

    // Extract target username
    char target_username[MAX_USERNAME_LEN];
    if (extract_username(to, target_username, sizeof(target_username)) != 0) {
        printf("[SIP_FAKE]   ERROR: Failed to extract target username\n");
        return;
    }

    // Find target registration
    registration_t* target = find_registration(target_username);
    if (!target) {
        printf("[SIP_FAKE]   ERROR: Target %s not registered\n", target_username);
        return;
    }

    printf("[SIP_FAKE]   Routing ACK to %s (%s:%u)\n",
           target_username, target->ip, target->port);

    // Forward ACK to target
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(target->port);
    inet_pton(AF_INET, target->ip, &target_addr.sin_addr);

    if (sendto(sock, sip_msg, strlen(sip_msg), 0,
               (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
        printf("[SIP_FAKE]   ERROR: Failed to forward ACK: %s\n", strerror(errno));
        return;
    }

    printf("[SIP_FAKE]   Forwarded ACK to %s\n", target_username);

    (void)client_addr;  // ACK is forwarded, not responded to
}

/**
 * @brief Handle BYE request
 */
static void handle_bye(
    int sock,
    const char* sip_msg,
    struct sockaddr_in* client_addr)
{
    printf("[SIP_FAKE] Handling BYE\n");

    // Extract headers
    const char* from = extract_from(sip_msg);
    const char* to = extract_to(sip_msg);
    const char* call_id = extract_call_id(sip_msg);
    const char* cseq = extract_cseq(sip_msg);
    const char* via = extract_via(sip_msg);

    if (!from || !to || !call_id || !cseq || !via) {
        printf("[SIP_FAKE]   ERROR: Missing required headers\n");
        return;
    }

    // Send 200 OK to sender
    char response[MAX_PACKET_SIZE];
    int response_len = generate_response(
        response, sizeof(response),
        200, "OK",
        via, from, to, call_id, cseq, NULL);

    if (sendto(sock, response, response_len, 0,
               (struct sockaddr*)client_addr, sizeof(*client_addr)) < 0) {
        printf("[SIP_FAKE]   ERROR: Failed to send response: %s\n", strerror(errno));
        return;
    }

    printf("[SIP_FAKE]   Sent 200 OK for BYE\n");

    // Forward BYE to other party
    char target_username[MAX_USERNAME_LEN];
    if (extract_username(to, target_username, sizeof(target_username)) == 0) {
        registration_t* target = find_registration(target_username);
        if (target) {
            printf("[SIP_FAKE]   Forwarding BYE to %s (%s:%u)\n",
                   target_username, target->ip, target->port);

            struct sockaddr_in target_addr;
            memset(&target_addr, 0, sizeof(target_addr));
            target_addr.sin_family = AF_INET;
            target_addr.sin_port = htons(target->port);
            inet_pton(AF_INET, target->ip, &target_addr.sin_addr);

            sendto(sock, sip_msg, strlen(sip_msg), 0,
                   (struct sockaddr*)&target_addr, sizeof(target_addr));
        }
    }
}

/**
 * @brief Handle SIP response (200 OK, etc.)
 */
static void handle_response(
    int sock,
    const char* sip_msg,
    struct sockaddr_in* client_addr)
{
    printf("[SIP_FAKE] Handling SIP response\n");

    // Extract To header to find original sender
    const char* to = extract_to(sip_msg);
    if (!to) {
        printf("[SIP_FAKE]   ERROR: Missing To header\n");
        return;
    }

    // Extract target username (original sender)
    char target_username[MAX_USERNAME_LEN];
    if (extract_username(to, target_username, sizeof(target_username)) != 0) {
        printf("[SIP_FAKE]   ERROR: Failed to extract target username\n");
        return;
    }

    // Find target registration
    registration_t* target = find_registration(target_username);
    if (!target) {
        printf("[SIP_FAKE]   WARNING: Target %s not registered, dropping response\n",
               target_username);
        return;
    }

    printf("[SIP_FAKE]   Routing response to %s (%s:%u)\n",
           target_username, target->ip, target->port);

    // Forward response to target
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(target->port);
    inet_pton(AF_INET, target->ip, &target_addr.sin_addr);

    if (sendto(sock, sip_msg, strlen(sip_msg), 0,
               (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
        printf("[SIP_FAKE]   ERROR: Failed to forward response: %s\n", strerror(errno));
        return;
    }

    printf("[SIP_FAKE]   Forwarded response to %s\n", target_username);

    (void)client_addr;
}

/* ========================================
 * Main Server Loop
 * ======================================== */

/**
 * @brief Main function
 */
int main(void)
{
    printf("========================================\n");
    printf("SIP Fake Server\n");
    printf("========================================\n");
    printf("Listening on UDP port %d\n", SIP_PORT);
    printf("========================================\n\n");
    fflush(stdout);

    // Initialize registration table
    memset(g_registrations, 0, sizeof(g_registrations));

    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        printf("ERROR: Failed to create socket: %s\n", strerror(errno));
        return 1;
    }

    // Allow address reuse
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        printf("WARNING: Failed to set SO_REUSEADDR: %s\n", strerror(errno));
    }

    // Bind to port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SIP_PORT);

    if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("ERROR: Failed to bind socket: %s\n", strerror(errno));
        fflush(stdout);
        close(sock);
        return 1;
    }

    printf("Server started successfully\n\n");
    fflush(stdout);

    // Main loop
    char buffer[MAX_PACKET_SIZE];
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;

    while (1) {
        // Receive packet
        client_addr_len = sizeof(client_addr);
        ssize_t recv_len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                                    (struct sockaddr*)&client_addr, &client_addr_len);

        if (recv_len < 0) {
            printf("ERROR: recvfrom failed: %s\n", strerror(errno));
            continue;
        }

        buffer[recv_len] = '\0';

        // Get client info
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        uint16_t client_port = ntohs(client_addr.sin_port);

        printf("[SIP_FAKE] Received %zd bytes from %s:%u\n", recv_len, client_ip, client_port);
        fflush(stdout);

        // Print first line of SIP message
        const char* first_line_end = strchr(buffer, '\r');
        if (!first_line_end) {
            first_line_end = strchr(buffer, '\n');
        }
        if (first_line_end) {
            printf("[SIP_FAKE]   %.*s\n", (int)(first_line_end - buffer), buffer);
            fflush(stdout);
        }

        // Determine message type and handle
        if (strncmp(buffer, "REGISTER ", 9) == 0) {
            handle_register(sock, buffer, &client_addr);
        } else if (strncmp(buffer, "INVITE ", 7) == 0) {
            handle_invite(sock, buffer, &client_addr);
        } else if (strncmp(buffer, "ACK ", 4) == 0) {
            handle_ack(sock, buffer, &client_addr);
        } else if (strncmp(buffer, "BYE ", 4) == 0) {
            handle_bye(sock, buffer, &client_addr);
        } else if (strncmp(buffer, "SIP/2.0 ", 8) == 0) {
            // SIP response
            handle_response(sock, buffer, &client_addr);
        } else {
            // Unknown method
            const char* method = extract_method(buffer);
            printf("[SIP_FAKE]   WARNING: Unhandled method: %s\n",
                   method ? method : "UNKNOWN");
        }

        printf("\n");
    }

    close(sock);
    return 0;
}
