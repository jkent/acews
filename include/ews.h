// SPDX-License-Identifier: MIT
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "ews_config.h"


////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews_server Server Instances
/// @{

/// web server type
typedef struct ews ews_t;

/// web server configuration type
typedef struct ews_config ews_config_t;

/// web server configuration struct
struct ews_config {
    /// millisecond idle timeout
    int idle_timeout;

#if CONFIG_EWS_HTTP_CLIENTS > 0 || defined(__DOXYGEN__)
    /// port to use for http listen socket
    uint16_t http_listen_port;
    /// backlog for http listen socket
    uint16_t http_listen_backlog;
#endif

#if CONFIG_EWS_HTTPS_CLIENTS > 0 || defined(__DOXYGEN__)
    /// port to use for https listen socket
    uint16_t https_listen_port;
    /// backlog for https listen socket
    uint16_t https_listen_backlog;

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
/// @defgroup ews_routes Routes
/// @{

/// http session type
typedef struct ews_http ews_http_t;

/// route status type
typedef int8_t ews_status_t;

/// route handler type
typedef ews_status_t (*ews_handler_t)(ews_http_t *http);

/// route status return values
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
/// @param[in] ews sever instance
/// @param[in] pattern a glob-like path matching string (not copied!)
/// @param[in] handler a route handler
/// @param[in] argc the number of arguments to the route handler
/// @param[in] ... arguments passed to the route handler
/// @return 0 if successful, -1 otherwise
int ews_routes_append(ews_t *ews, const char *pattern, ews_handler_t handler,
        size_t argc, ...);

/// clear the list of route handlers
/// @param[inout] ews server instance
void ews_routes_clear(ews_t *ews);

/// a demo route handler to test functionality
/// @param[inout] http session instance
/// @return route handler status
ews_status_t ews_routes_test_handler(ews_http_t *http);

/// a route handler that serves files from a directory
/// @param[inout] http session instance
/// @return route handler status
ews_status_t ews_routes_stdio_get_handler(ews_http_t *http);

/// a route handler that redirects to paths with a trailing slash
/// @param[inout] http session instance
/// @return route handler status
ews_status_t ews_routes_directory_rediret_handler(ews_http_t *http);

/// @}
////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews_http HTTP Sessions
/// @{

/// http session type
typedef struct ews_http ews_http_t;

/// http ops type
typedef struct ews_http_ops ews_http_ops_t;

/// http session data type
typedef struct ews_sess ews_sess_t;

/// http route type
typedef struct ews_route ews_route_t;

/// http state type
typedef uint8_t ews_state_t;

/// http flags type
typedef uint16_t ews_flags_t;

/// session states
enum {
    /// request line and headers have been fetched, may inspect headers with
    /// http->ops->get_hdr(); SHOULD return with EWS_STATUS_MATCH or
    /// EWS_STATUS_NOMATCH; MUST clean up any user data if EWS_STATUS_NOMATCH
    /// is returned
    EWS_STATE_REQ           = 0x00,
    /// a chunk of data has been fetched, waiting on http->ops->recv(); SHOULD
    /// return with EWS_STATUS_NEXT
    EWS_STATE_REQ_BDY       = 0x01,
    /// multipart boundary and multipart headers have been fetched, may inspect
    /// headers with http->ops->get_hdr(); SHOULD rturn with EWS_STATUS_NEXT
    EWS_STATE_REQ_MP        = 0x04,
    /// a chunk of multipart data has been fetched, waiting on
    /// http->ops->recv(); SHOULD return with EWS_STATUS_NEXT
    EWS_STATE_REQ_MP_BDY    = 0x05,
    /// a response is ready to be started, http->ops->start_rsp() SHOULD be
    /// called and any headers can be sent using http->ops->send_hdr() or
    /// http->ops->sendf_hdr(); http->sess->state_count can be used to break
    /// up send_hdr calls; SHOULD return with EWS_STATUS_NEXT or EWS_STATUS_MORE
    EWS_STATE_RSP           = 0x08,
    /// a response body is ready to be sent, http->ops->send() or
    /// http->ops->sendf() SHOULD be called; http->sess->state_ount can be used
    /// to break up long responses; SHOULD return with EWS_STATUS_NEXT or
    /// EWS_STATUS_MORE
    EWS_STATE_RSP_BDY       = 0x09,
    /// the session is about to be cleaned up, this state is always called at
    /// the end of a route that returned EWS_STATUS_MATCH;  MUST clean up any
    /// user data
    EWS_STATE_FIN           = 0xFF,
};

/// session flags
enum {
    /// request is HTTP/1.1, HTTP/1.0 otherwise
    EWS_FLAGS_HTTP11        = (1 <<  0),
};

/// http ops struct
struct ews_http_ops {
    /// get header, starting with the pesudo headers
    /// :method, :path, :scheme, :x-path, and :x-query
    /// @param[inout] http session instance
    /// @param[out] name header name pointer
    /// @param[out] value header value pointer
    /// @param[out] value_len header value_len pointer, can be NULL
    /// @returns < 0 on error, 0 no more headers, > 0 header valid
    int (*get_hdr)(ews_http_t *http, const char **name, const char **value,
            size_t *value_len);

    /// recv data
    /// @param[inout] http session instance
    /// @param[out] buf buffer, can be null to discard data
    /// @param[in] buf_sz buffer size, -1 for all (useful for discarding)
    /// @returns < 0 on error, 0 try again later, > 0 bytes received
    /// @note this function may not return the ammount of data requested, this
    ///       is expected behavior
    ssize_t (*recv)(ews_http_t *http, char *buf, ssize_t buf_sz);

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
    ssize_t (*send)(ews_http_t *http, const char *buf, ssize_t buf_sz);

    /// send formatted data
    /// @param[inout] http session instance
    /// @param[in] fmt format string
    /// @param[in] ... format arguments
    /// @returns < 0 on error, 0 try again later, > 0 num bytes transmitted
    /// @note this function tries best effort to send all the data, but can
    ///       timeout and will return error in that case.
    ssize_t (*sendf)(ews_http_t *http, const char *fmt, ...);
};

#if !defined(EWS_PRIVATE_DEFS)
/// http session struct (public view)
struct ews_http {
    /// http ops
    ews_http_ops_t *ops;
    /// current route handler
    const ews_route_t *route;
    /// http session data
    ews_sess_t *sess;
    /// user session data
    void *user;
};
#endif

#if !defined(EWS_PRIVATE_DEFS)
/// http session data struct (public view)
struct ews_sess {
    /// current session state
    const ews_state_t state;
    /// session flags
    const ews_flags_t flags;
    /// current state iteration
    const size_t state_count;
};
#endif

/// @}
////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews_ws Websockets
/// @{

#if CONFIG_EWS_WS_CLIENTS > 0 || CONFIG_EWS_WSS_CLIENTS > 0 || \
        defined(__DOXYGEN__)

/// websocket session type
typedef struct ews_ws ews_ws_t;

/// websocket ops type
typedef struct ews_ws_ops ews_ws_ops_t;

/// websocket loop type
typedef void (*ews_ws_loop_t)(ews_ws_t *ws);

/// websocket send/recv flags
enum {
    /// used to signal the end of a message
    EWS_WS_FLAG_FIN         = (1 <<  0),
    /// transfer is binary data instead of utf-8 text
    EWS_WS_FLAG_BIN         = (1 <<  1),
};

/// ws ops struct
struct ews_ws_ops {
    /// recv data
    /// @param[inout] ws session instance
    /// @param[out] buf buffer, can be null to discard data
    /// @param[in] buf_sz buffer size, -1 for all (useful for discarding)
    /// @returns < 0 on error, 0 try again later, > 0 bytes received
    /// @note this function may not return the ammount of data requested, this
    ///       is expected behavior
    ssize_t (*recv)(ews_ws_t *ws, int *flags, char *buf, ssize_t buf_sz);

    /// send data
    /// @param[inout] ws session instance
    /// @param[in] buf buffer
    /// @param[in] buf_sz buffer size, -1 for strlen()
    /// @returns < 0 on error, 0 try again later, > 0 num bytes transmitted
    /// @note this function tries best effort to send all the data, but can
    ///       timeout and will return error in that case.
    ssize_t (*send)(ews_ws_t *ws, int flags, const char *buf, ssize_t buf_sz);

    /// send formatted data
    /// @param[inout] ws session instance
    /// @param[in] fmt format string
    /// @param[in] ... format arguments
    /// @returns < 0 on error, 0 try again later, > 0 num bytes transmitted
    /// @note this function tries best effort to send all the data, but can
    ///       timeout and will return error in that case.
    ssize_t (*sendf)(ews_ws_t *ws, int flags, const char *fmt, ...);
};

#if !defined(EWS_PRIVATE_DEFS)
/// ws session struct (public view)
struct ews_ws {
    /// ws ops
    const ews_ws_ops_t *ops;
    /// route handler
    ews_route_t *route;
    /// user data
    void *user;
};
#endif

/// generate Sec-WebSocket-Accept for given Sec-WebSocket-Key
/// @param[out] out accept string
/// @param[in] out_len accept buffer size
/// @param[in] key key string
/// @return < 0 on error, 0 on success
int ews_ws_gen_accept(char *out, size_t out_len, const char *key);

/// a route handler that upgrades to a websocket
/// the route handler expects at least one argument: a function pointer for the
///     websocket's main loop
/// @param[inout] http session instance
/// @return route handler status
ews_status_t ews_ws_route_handler(ews_http_t *http);

#endif

/// @}
////////////////////////////////////////////////////////////////////////////////

#if defined(__cplusplus)
} // extern "C"
#endif
