/**
 * @file lws_auth.h
 * @brief SIP Digest Authentication API
 */

#ifndef __LWS_AUTH_H__
#define __LWS_AUTH_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================
 * Data structures
 * ======================================== */

/**
 * @brief Digest authentication challenge from server
 */
typedef struct {
    char realm[256];        /**< Authentication realm */
    char nonce[256];        /**< Server nonce */
    char algorithm[16];     /**< Algorithm (usually "MD5") */
    char qop[64];           /**< Quality of protection */
    char opaque[256];       /**< Opaque value */
} lws_auth_challenge_t;

/**
 * @brief User credentials
 */
typedef struct {
    const char* username;   /**< SIP username */
    const char* password;   /**< SIP password */
} lws_auth_credentials_t;

/**
 * @brief Digest authentication response
 */
typedef struct {
    char realm[256];        /**< Authentication realm */
    char nonce[256];        /**< Server nonce */
    char uri[512];          /**< Request URI */
    char response[33];      /**< Calculated MD5 response (32 hex chars + null) */
    char algorithm[16];     /**< Algorithm */
    char qop[64];           /**< Quality of protection */
    char cnonce[33];        /**< Client nonce */
    uint32_t nc;            /**< Nonce count */
    char opaque[256];       /**< Opaque value */
} lws_auth_response_t;

/* ========================================
 * Public API
 * ======================================== */

/**
 * @brief Parse WWW-Authenticate or Proxy-Authenticate header
 * @param challenge The challenge string (e.g., "Digest realm=\"example.com\", ...")
 * @param auth Output structure to store parsed challenge
 * @return 0 on success, -1 on failure
 */
int lws_auth_parse_challenge(const char* challenge, lws_auth_challenge_t* auth);

/**
 * @brief Generate digest authentication response
 * @param challenge Parsed authentication challenge
 * @param credentials User credentials
 * @param method SIP method (e.g., "REGISTER", "INVITE")
 * @param uri Request URI
 * @param response Output structure to store generated response
 * @return 0 on success, -1 on failure
 */
int lws_auth_generate_response(const lws_auth_challenge_t* challenge,
                               const lws_auth_credentials_t* credentials,
                               const char* method,
                               const char* uri,
                               lws_auth_response_t* response);

/**
 * @brief Build Authorization or Proxy-Authorization header value
 * @param response Generated authentication response
 * @param username SIP username
 * @param header Output buffer for header value
 * @param header_len Size of output buffer
 * @return 0 on success, -1 on failure
 */
int lws_auth_build_authorization_header(const lws_auth_response_t* response,
                                       const char* username,
                                       char* header,
                                       size_t header_len);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_AUTH_H__ */
