// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "ews_config.h"


////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews Web Server
/// @{

/// web server configuration type
typedef struct ews_config ews_config_t;

/// web server configuration struct
struct ews_config {
    /// millisecond idle timeout
    int idle_timeout;

#if CONFIG_EWS_HTTP_CLIENTS > 0 || defined(__DOXYGEN__)
    /// port to use for http listen socket
    int http_listen_port;
    /// backlog for http listen socket
    int http_listen_backlog;
#endif

#if CONFIG_EWS_HTTPS_CLIENTS > 0 || defined(__DOXYGEN__)
    /// port to use for https listen socket
    int https_listen_port;
    /// backlog for https listen socket
    int https_listen_backlog;

    /// https server certificiate
    const void *https_crt;
    /// https server certificate length
    size_t https_crt_len;
    /// https server private key
    const void *https_pk;
    /// https server private key length
    size_t https_pk_len;
#endif
};

/// web server type
typedef struct ews ews_t;

/// initialize and start web server
/// @param[in] config server configuration struct
/// @return web server instance
ews_t *ews_init(const ews_config_t *config);

/// stop and clean up web server instance
/// @param[in] ews web server instance
void ews_destroy(ews_t *ews);

#if CONFIG_EWS_HTTPS_CLIENTS > 0 || defined(__DOXYGEN__)
/// add a client certificate and enable certificate checking
/// @param[in] ews web server instance
/// @param[in] crt client certificate
/// @param[in] crt_len client certificate length
/// @return @b true if successful, @b false otherwise
bool ews_add_client_cert(ews_t *ews, const uint8_t *crt, size_t crt_len);
#endif

/// @}
////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews_sessions Sessions
/// @{

/// session state type
typedef enum ews_sess_state ews_sess_state_t;

/// session state enum
enum ews_sess_state  {
    /// request begin: user decides if request method and path are OK
    EWS_SESS_REQUEST_BEGIN      =  0 | 0x00,
    /// request header: called once per request header
    EWS_SESS_REQUEST_HEADER     =  1 | 0x00,
    /// request body: called until user reads or ignores all request data
    EWS_SESS_REQUEST_BODY       =  2 | 0x00,

    /// response begin: user sends http status
    EWS_SESS_RESPONSE_BEGIN     =  0 | 0x10,
    /// response header: user sends one header and signals next state when ready
    EWS_SESS_RESPONSE_HEADER    =  1 | 0x10,
    /// response body:user sends body and signals next state (done) when ready
    EWS_SESS_RESPONSE_BODY      =  2 | 0x10,

    /// finalize: called if state is > EWS_SESS_REQUEST_BEGIN
    EWS_HTTP_FINALIZE           = 15 | 0x30,
};

/// session methods type
typedef enum ews_sess_methods ews_sess_methods_t;

/// session ops type
typedef struct ews_sess_ops ews_sess_ops_t;

/// session data type
typedef struct ews_sess_data ews_sess_data_t;

/// session type
typedef struct ews_sess ews_sess_t;

/// socket type
typedef struct ews_sock ews_sock_t;

/// session methods enum
enum ews_sess_methods {
    EWS_SESS_METHOD_OTHER,
    EWS_SESS_METHOD_CONNECT,
    EWS_SESS_METHOD_DELETE,
    EWS_SESS_METHOD_GET,
    EWS_SESS_METHOD_HEAD,
    EWS_SESS_METHOD_OPTIONS,
    EWS_SESS_METHOD_PATCH,
    EWS_SESS_METHOD_POST,
    EWS_SESS_METHOD_PUT,
    EWS_SESS_METHOD_TRACE,
};

/// session operations struct
struct ews_sess_ops {
    /// session recv data
    /// @param[in] sess session
    /// @param[out] buf buffer
    /// @param[in] len buffer size
    /// @returns -1 on error, otherwise written size
    ssize_t (*recv)(ews_sess_t *sess, void *buf, size_t len);
    /// session send data
    /// @param[in] sess session
    /// @param[in] buf buffer
    /// @param[in] len buffer size
    /// @returns -1 on error, otherwise read size
    ssize_t (*send)(ews_sess_t *sess, const void *buf, size_t len);
    /// session send printf formatted data
    /// @param[in] sess session
    /// @param[in] fmt format string
    /// @param[in] ... format arguments
    void (*sendf)(ews_sess_t *sess, const char *fmt, ...);
    /// session send status
    /// @param[in] sess session
    /// @param[in] code http response code
    /// @param[in] msg http response message
    void (*status)(ews_sess_t *sess, int code, const char *msg);
    /// session send error
    /// @param[in] sess session
    /// @param[in] code http response code
    /// @param[in] msg error message
    void (*error)(ews_sess_t *sess, int code, const char *msg);
    /// session send header
    /// @param[in] sess session
    /// @param[in] name header name
    /// @param[in] value header value
    void (*header)(ews_sess_t *sess, const char *name, const char *value);
};

/// session data struct
struct ews_sess_data {
    union {
        /// http request path, only valid during request_begin
        char *path;
        /// header name, only valid during request_header
        char *name;
        /// body chunk, only valid during request_body
        char *chunk;
    };
    union {
        /// http request path length, only valid during request_begin
        size_t path_len;
        /// header name length, only valid during request_header
        size_t name_len;
        /// body chunk length, only valid during request_body
        size_t chunk_len;
    };
    union {
        /// http request query string, only valid during request_begin
        char *query;
        /// header value, only valid during request_header
        char *value;
    };
    union {
        /// http_request query string length, only valid during request_begin
        size_t query_len;
        /// header value length, only valid during request_header
        size_t value_len;
    };
    /// http request method
    uint8_t method;
};

/// session struct
struct ews_sess {
    /// socket
    ews_sock_t *sock;
    /// session ops
    const ews_sess_ops_t *ops;
    /// session data
    ews_sess_data_t data;
};

/// @}
////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews_routes Routes
/// @{

/// route status type
typedef enum ews_route_status ews_route_status_t;

/// route status enum
enum ews_route_status {
    /// route error status, all states
    EWS_ROUTE_STATUS_ERROR = -1,
    /// route close status, all states
    EWS_ROUTE_STATUS_CLOSE,

    /// route not found status, for request begin state
    EWS_ROUTE_STATUS_NOT_FOUND,
    /// route found status, for request begin state
    EWS_ROUTE_STATUS_FOUND,

    /// route next state, for request and resposne states
    EWS_ROUTE_STATUS_NEXT,
    /// route done state, for request and response states
    EWS_ROUTE_STATUS_DONE,

    /// route more state, for response states except response begin
    EWS_ROUTE_STATUS_MORE,
};

/// route handler type
typedef ews_route_status_t (*ews_route_handler_t)(ews_sess_t *sess,
        ews_sess_state_t state);

/// append a route to the list of route handlers
/// @param[in] ews web sever instance
/// @param[in] pattern a glob-like path matching string (not copied!)
/// @param[in] handler a route handler
/// @param[in] argc the number of arguments to the route handler
/// @param[inout] ... arguments passed to/from the route handler
/// @return @b true if successful, @b false otherwise
bool ews_route_append(ews_t *ews, const char *pattern,
        ews_route_handler_t handler, size_t argc, ...);

/// clear the list of route handlers
/// @param[in] ews webs server instance
void ews_route_clear(ews_t *ews);

/// a demo route handler to test functionality
/// @param[in] sess session instance
/// @param[in] state current session state
/// @return route handler status
ews_route_status_t ews_route_test_handler(ews_sess_t *sess,
        ews_sess_state_t state);

/// @}
////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
} // extern "C"
#endif
