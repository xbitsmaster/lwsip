/**
 * @file lws_auth.c
 * @brief SIP Digest Authentication implementation (RFC 2617, RFC 3261)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#define MD5_CTX CC_MD5_CTX
#define MD5_Init CC_MD5_Init
#define MD5_Update CC_MD5_Update
#define MD5_Final CC_MD5_Final
#define MD5_DIGEST_LENGTH CC_MD5_DIGEST_LENGTH
#else
/* For Linux/embedded, we'll need to add alternative MD5 implementation */
#error "MD5 implementation needed for non-macOS platforms"
#endif

#include "lws_auth.h"
#include "lws_log.h"

/* ========================================
 * Internal helper functions
 * ======================================== */

/**
 * @brief Convert binary MD5 hash to hex string
 */
static void md5_to_hex(const unsigned char* md5, char* hex_output) {
    int i;
    for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf(hex_output + (i * 2), "%02x", md5[i]);
    }
    hex_output[32] = '\0';
}

/**
 * @brief Calculate MD5 hash of a string
 */
static void calculate_md5(const char* input, char* output) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_CTX ctx;

    MD5_Init(&ctx);
    MD5_Update(&ctx, input, strlen(input));
    MD5_Final(digest, &ctx);

    md5_to_hex(digest, output);
}

/**
 * @brief Generate random client nonce
 */
static void generate_cnonce(char* cnonce, size_t len) {
    static const char charset[] = "0123456789abcdef";
    size_t i;
    unsigned int seed = (unsigned int)time(NULL);

    for (i = 0; i < len - 1; i++) {
        cnonce[i] = charset[rand_r(&seed) % (sizeof(charset) - 1)];
    }
    cnonce[len - 1] = '\0';
}

/**
 * @brief Parse a parameter from Digest challenge string
 * @param challenge The WWW-Authenticate header value
 * @param param Parameter name to find (e.g., "realm", "nonce")
 * @param value Output buffer for parameter value
 * @param value_len Size of output buffer
 * @return 0 on success, -1 on failure
 */
static int parse_digest_param(const char* challenge, const char* param,
                              char* value, size_t value_len) {
    const char* start;
    const char* end;
    size_t param_name_len = strlen(param);

    /* Find parameter name */
    start = strstr(challenge, param);
    if (!start) {
        return -1;
    }

    /* Skip parameter name and '=' */
    start += param_name_len;
    while (*start == ' ' || *start == '=') {
        start++;
    }

    /* Handle quoted values */
    int quoted = 0;
    if (*start == '"') {
        quoted = 1;
        start++;
    }

    /* Find end of value */
    if (quoted) {
        end = strchr(start, '"');
    } else {
        end = start;
        while (*end && *end != ',' && *end != ' ') {
            end++;
        }
    }

    if (!end || end <= start) {
        return -1;
    }

    /* Copy value */
    size_t len = end - start;
    if (len >= value_len) {
        len = value_len - 1;
    }

    memcpy(value, start, len);
    value[len] = '\0';

    return 0;
}

/* ========================================
 * Public API implementation
 * ======================================== */

int lws_auth_parse_challenge(const char* challenge, lws_auth_challenge_t* auth) {
    if (!challenge || !auth) {
        return -1;
    }

    memset(auth, 0, sizeof(lws_auth_challenge_t));

    /* Parse realm */
    if (parse_digest_param(challenge, "realm", auth->realm, sizeof(auth->realm)) < 0) {
        lws_log_error(0, "[AUTH] Failed to parse realm\n");
        return -1;
    }

    /* Parse nonce */
    if (parse_digest_param(challenge, "nonce", auth->nonce, sizeof(auth->nonce)) < 0) {
        lws_log_error(0, "[AUTH] Failed to parse nonce\n");
        return -1;
    }

    /* Parse algorithm (optional, default to MD5) */
    if (parse_digest_param(challenge, "algorithm", auth->algorithm, sizeof(auth->algorithm)) < 0) {
        strcpy(auth->algorithm, "MD5");
    }

    /* Parse qop (optional) */
    if (parse_digest_param(challenge, "qop", auth->qop, sizeof(auth->qop)) < 0) {
        auth->qop[0] = '\0';
    }

    /* Parse opaque (optional) */
    parse_digest_param(challenge, "opaque", auth->opaque, sizeof(auth->opaque));

    lws_log_info("[AUTH] Parsed challenge: realm=%s, nonce=%s, algorithm=%s, qop=%s\n",
                 auth->realm, auth->nonce, auth->algorithm, auth->qop);

    return 0;
}

int lws_auth_generate_response(const lws_auth_challenge_t* challenge,
                               const lws_auth_credentials_t* credentials,
                               const char* method,
                               const char* uri,
                               lws_auth_response_t* response) {
    if (!challenge || !credentials || !method || !uri || !response) {
        return -1;
    }

    memset(response, 0, sizeof(lws_auth_response_t));

    /* Copy basic info */
    strncpy(response->realm, challenge->realm, sizeof(response->realm) - 1);
    strncpy(response->nonce, challenge->nonce, sizeof(response->nonce) - 1);
    strncpy(response->algorithm, challenge->algorithm, sizeof(response->algorithm) - 1);
    strncpy(response->opaque, challenge->opaque, sizeof(response->opaque) - 1);
    strncpy(response->uri, uri, sizeof(response->uri) - 1);

    /* Generate cnonce if qop is present */
    if (challenge->qop[0] != '\0') {
        generate_cnonce(response->cnonce, sizeof(response->cnonce));
        strncpy(response->qop, challenge->qop, sizeof(response->qop) - 1);
        response->nc = 1;  /* First request with this nonce */
    }

    /* Calculate HA1 = MD5(username:realm:password) */
    char ha1_input[512];
    char ha1[33];
    snprintf(ha1_input, sizeof(ha1_input), "%s:%s:%s",
             credentials->username, challenge->realm, credentials->password);
    calculate_md5(ha1_input, ha1);

    /* Calculate HA2 = MD5(method:uri) */
    char ha2_input[512];
    char ha2[33];
    snprintf(ha2_input, sizeof(ha2_input), "%s:%s", method, uri);
    calculate_md5(ha2_input, ha2);

    /* Calculate response */
    char response_input[1024];
    if (challenge->qop[0] != '\0') {
        /* With qop: response = MD5(HA1:nonce:nc:cnonce:qop:HA2) */
        snprintf(response_input, sizeof(response_input), "%s:%s:%08x:%s:%s:%s",
                 ha1, challenge->nonce, response->nc, response->cnonce,
                 response->qop, ha2);
    } else {
        /* Without qop: response = MD5(HA1:nonce:HA2) */
        snprintf(response_input, sizeof(response_input), "%s:%s:%s",
                 ha1, challenge->nonce, ha2);
    }

    calculate_md5(response_input, response->response);

    lws_log_info("[AUTH] Generated response: %s\n", response->response);
    lws_log_debug("[AUTH] HA1=%s, HA2=%s\n", ha1, ha2);

    return 0;
}

int lws_auth_build_authorization_header(const lws_auth_response_t* response,
                                       const char* username,
                                       char* header,
                                       size_t header_len) {
    if (!response || !username || !header) {
        return -1;
    }

    int written = 0;

    /* Start with Digest */
    written = snprintf(header, header_len,
                      "Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", "
                      "uri=\"%s\", response=\"%s\", algorithm=%s",
                      username, response->realm, response->nonce,
                      response->uri, response->response, response->algorithm);

    if (written < 0 || (size_t)written >= header_len) {
        return -1;
    }

    /* Add qop, nc, cnonce if present */
    if (response->qop[0] != '\0') {
        int extra = snprintf(header + written, header_len - written,
                           ", qop=%s, nc=%08x, cnonce=\"%s\"",
                           response->qop, response->nc, response->cnonce);
        if (extra < 0) {
            return -1;
        }
        written += extra;
    }

    /* Add opaque if present */
    if (response->opaque[0] != '\0') {
        int extra = snprintf(header + written, header_len - written,
                           ", opaque=\"%s\"", response->opaque);
        if (extra < 0) {
            return -1;
        }
        written += extra;
    }

    lws_log_debug("[AUTH] Authorization header: %s\n", header);

    return 0;
}
