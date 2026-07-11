#ifndef TWM_HTTP_SERVER_H
#define TWM_HTTP_SERVER_H

#include "config.h"
#include <stddef.h>

#define HTTP_MAX_HEADERS 32
#define HTTP_MAX_HEADER_LEN 512
#define HTTP_MAX_PATH_LEN 1024
#define HTTP_MAX_BODY 65536

typedef struct {
    char name[64];
    char value[HTTP_MAX_HEADER_LEN];
} http_header_t;

typedef struct {
    char method[8];
    char path[HTTP_MAX_PATH_LEN];   /* decoded, without query string */
    char query[HTTP_MAX_PATH_LEN];  /* raw query string, may be empty */

    http_header_t headers[HTTP_MAX_HEADERS];
    int header_count;

    char cookie_session[128];       /* extracted "session" cookie, if any */

    char body[HTTP_MAX_BODY];
    size_t body_len;
} http_request_t;

typedef struct {
    int status;
    char content_type[64];
    char *body;        /* heap-allocated; server frees after send */
    size_t body_len;
    char set_cookie[256]; /* optional Set-Cookie header value, empty = none */
} http_response_t;

/* Convenience helpers to fill an http_response_t. */
void http_respond_json(http_response_t *res, int status, const char *json_body);
void http_respond_text(http_response_t *res, int status, const char *content_type,
                        const char *body);
void http_respond_file(http_response_t *res, int status, const char *content_type,
                        const char *data, size_t len);
void http_respond_json_error(http_response_t *res, int status, const char *message);

/* Application-supplied request handler (implemented in routes.c). */
typedef void (*http_dispatch_fn)(const twm_config_t *cfg, http_request_t *req,
                                  http_response_t *res);

/* Starts the HTTP server (blocking call — runs the accept loop). */
int http_server_run(const twm_config_t *cfg, http_dispatch_fn dispatch);

#endif
