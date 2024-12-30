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
/// @defgroup types Types
/// @{

/// web server configuration type
typedef struct ews_config ews_config_t;

/// web server type
typedef struct ews ews_t;

/// http data type
typedef struct ews_http ews_http_t;

/// http route type
typedef struct ews_route ews_route_t;

/// session state type
typedef uint8_t ews_state_t;

/// session flags type
typedef uint16_t ews_http_flags_t;

/// methods type
typedef uint8_t ews_method_t;

/// http ops type
typedef struct ews_http_ops ews_http_ops_t;

/// http data type
typedef struct ews_http_data ews_http_data_t;

/// http session type
typedef struct ews_http_sess ews_http_sess_t;

/// route status type
typedef int8_t ews_status_t;

/// route handler type
typedef ews_status_t (*ews_handler_t)(ews_http_t *http, ews_http_sess_t *sess);

/// @}
////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews Server
/// @{

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
    int https_crt_len;
    /// https server private key
    const void *https_pk;
    /// https server private key length
    int https_pk_len;
#endif
};

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
bool ews_add_client_cert(ews_t *ews, const uint8_t *crt, int crt_len);
#endif

/// @}
////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews_http HTTP
/// @{

/// session states
#define EWS_STATE_REQ           0x00
#define EWS_STATE_REQ_BDY       0x01
#define EWS_STATE_REQ_MP        0x04
#define EWS_STATE_REQ_MP_BDY    0x05

#define EWS_STATE_RSP           0x08
#define EWS_STATE_RSP_BDY       0x09
#define EWS_STATE_FIN           0xFF

#define EWS_FLAGS_HTTP11        (1 <<  0)

/// http ops struct
struct ews_http_ops {
    /// get header, starting with pesudo headers :method, :path, and :scheme
    /// @param[inout] http session instance
    /// @param[in] name header name pointer
    /// @param[in] value header value pointer
    /// @returns < 0 on error, 0 no more headers, > 0 header valid
    int (*get_hdr)(ews_http_t *http, const char **name, const char **value);

    /// recv data
    /// @param[inout] http session instance
    /// @param[out] buf buffer, can be null to discard data
    /// @param[in] buf_sz buffer size, -1 for all (useful for discarding)
    /// @returns < 0 on error, 0 try again later, > 0 bytes received
    /// @note this function may not return the ammount of data requested, this
    ///       is expected behavior
    int (*recv)(ews_http_t *http, char *buf, int buf_sz);

    /// send http status
    /// @param[inout] http session instance
    /// @param[in] code http status code
    /// @param[in] status http status string
    /// @returns < 0 on error, 0 try again later, > 0 rsp header sent
    /// @note this function tries best effort to send all the data, but can
    ///       timeout and will return error in that case.
    int (*start_rsp)(ews_http_t *http, int code, const char *status);

    /// send header
    /// @param[inout] http session instance
    /// @param[in] name header name
    /// @param[in] value header value format string
    /// @returns < 0 on error, 0 try again later, > 0 header sent
    int (*send_hdr)(ews_http_t *http, const char *name, const char *value);

    /// send header with formatted value
    /// @param[inout] http session instance
    /// @param[in] name header name
    /// @param[in] fmt header value format string
    /// @param[in] ... header value format arguments
    /// @returns < 0 on error, 0 try again later, > 0 header sent
    int (*sendf_hdr)(ews_http_t *http, const char *name, const char *fmt, ...);

    /// send data
    /// @param[inout] http session instance
    /// @param[in] buf buffer
    /// @param[in] buf_sz buffer size, -1 for strlen()
    /// @returns < 0 on error, 0 try again later, > 0 num bytes transmitted
    /// @note this function tries best effort to send all the data, but can
    ///       timeout and will return error in that case.
    int (*send)(ews_http_t *http, const char *buf, int buf_sz);

    /// send formatted data
    /// @param[inout] http session instance
    /// @param[in] fmt format string
    /// @param[in] ... format arguments
    /// @returns < 0 on error, 0 try again later, > 0 num bytes transmitted
    /// @note this function tries best effort to send all the data, but can
    ///       timeout and will return error in that case.
    int (*sendf)(ews_http_t *http, const char *fmt, ...);
};

#ifndef EWS_PRIVATE_DEFS
/// http struct (public view)
struct ews_http {
    /// http ops
    ews_http_ops_t *ops;
};
#endif

#ifndef EWS_PRIVATE_DEFS
/// http session struct (public view)
struct ews_http_sess {
    const ews_route_t *route;
    const ews_state_t state;
    const ews_http_flags_t flags;
    const int state_count;
};
#endif

/// @}
////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews_routes Routes
/// @{

enum {
    /// error status, any state
    EWS_STATUS_ERROR = -2,
    /// close status, any state
    EWS_STATUS_CLOSE,
    /// no match, only for EWS_STATE_REQ
    EWS_STATUS_NOMATCH,
    /// match, only for EWS_STATE_REQ
    EWS_STATUS_MATCH,
    /// more, for all states except EWS_STATE_REQ and EWS_STATE_FIN
    EWS_STATUS_MORE,
    /// skip, for all states except EWS_STATE_REQ
    EWS_STATUS_SKIP,
    /// next, for all states except EWS_STATE_REQ
    EWS_STATUS_NEXT,
    /// done, for all states except EWS_STATE_REQ
    EWS_STATUS_DONE,
};

/// append a route to the list of route handlers
/// @param[in] ews web sever instance
/// @param[in] pattern a glob-like path matching string (not copied!)
/// @param[in] handler a route handler
/// @param[in] argc the number of arguments to the route handler
/// @param[inout] ... arguments passed to/from the route handler
/// @return @b true if successful, @b false otherwise
bool ews_routes_append(ews_t *ews, const char *pattern, ews_handler_t handler,
        int argc, ...);

/// clear the list of route handlers
/// @param[in] ews webs server instance
void ews_routes_clear(ews_t *ews);

/// a demo route handler to test functionality
/// @param[in] sess session instance
/// @param[in] state current session state
/// @return route handler status
ews_status_t ews_routes_test_handler(ews_http_t *http, ews_http_sess_t *sess);

/// @}
////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
} // extern "C"
#endif
