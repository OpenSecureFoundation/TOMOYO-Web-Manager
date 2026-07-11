#include "http_server.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define RECV_CHUNK 4096

typedef struct {
    int fd;
    const twm_config_t *cfg;
    http_dispatch_fn dispatch;
} conn_ctx_t;

/* ---- response helpers ---- */

void http_respond_json(http_response_t *res, int status, const char *json_body) {
    res->status = status;
    snprintf(res->content_type, sizeof(res->content_type), "application/json; charset=utf-8");
    res->body_len = strlen(json_body);
    res->body = xmalloc(res->body_len + 1);
    memcpy(res->body, json_body, res->body_len + 1);
}

void http_respond_text(http_response_t *res, int status, const char *content_type,
                        const char *body) {
    res->status = status;
    snprintf(res->content_type, sizeof(res->content_type), "%s", content_type);
    res->body_len = strlen(body);
    res->body = xmalloc(res->body_len + 1);
    memcpy(res->body, body, res->body_len + 1);
}

void http_respond_file(http_response_t *res, int status, const char *content_type,
                        const char *data, size_t len) {
    res->status = status;
    snprintf(res->content_type, sizeof(res->content_type), "%s", content_type);
    res->body_len = len;
    res->body = xmalloc(len + 1);
    memcpy(res->body, data, len);
    res->body[len] = '\0';
}

void http_respond_json_error(http_response_t *res, int status, const char *message) {
    char esc[512];
    json_escape(message, esc, sizeof(esc));
    char buf[600];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", esc);
    http_respond_json(res, status, buf);
}

/* ---- request parsing ---- */

static void extract_session_cookie(http_request_t *req) {
    req->cookie_session[0] = '\0';
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].name, "Cookie") != 0) continue;
        const char *p = strstr(req->headers[i].value, "session=");
        if (!p) continue;
        p += strlen("session=");
        size_t o = 0;
        while (*p && *p != ';' && o + 1 < sizeof(req->cookie_session)) {
            req->cookie_session[o++] = *p++;
        }
        req->cookie_session[o] = '\0';
        break;
    }
}

static long find_content_length(const http_request_t *req) {
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].name, "Content-Length") == 0) {
            return atol(req->headers[i].value);
        }
    }
    return 0;
}

/* Reads a full HTTP request (headers + body) off fd into *req.
 * Returns 0 on success, -1 on error/EOF/malformed request. */
static int read_request(int fd, http_request_t *req) {
    memset(req, 0, sizeof(*req));

    size_t cap = 8192, len = 0;
    char *buf = xmalloc(cap);

    char *header_end = NULL;
    for (;;) {
        if (len + RECV_CHUNK > cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return -1; }
            buf = nb;
        }
        ssize_t n = recv(fd, buf + len, RECV_CHUNK, 0);
        if (n <= 0) { free(buf); return -1; }
        len += (size_t)n;
        buf[len] = '\0';
        header_end = strstr(buf, "\r\n\r\n");
        if (header_end) break;
        if (len > HTTP_MAX_BODY) { free(buf); return -1; } /* header section too large */
    }

    size_t header_len = (size_t)(header_end - buf) + 4;

    /* --- request line --- */
    char *line_end = strstr(buf, "\r\n");
    if (!line_end) { free(buf); return -1; }
    *line_end = '\0';

    char method[8] = {0}, full_path[HTTP_MAX_PATH_LEN] = {0};
    if (sscanf(buf, "%7s %1023s", method, full_path) != 2) { free(buf); return -1; }
    snprintf(req->method, sizeof(req->method), "%s", method);

    char *qmark = strchr(full_path, '?');
    if (qmark) {
        *qmark = '\0';
        snprintf(req->query, sizeof(req->query), "%s", qmark + 1);
    }
    char decoded[HTTP_MAX_PATH_LEN];
    url_decode(full_path, decoded);
    snprintf(req->path, sizeof(req->path), "%s", decoded);

    /* --- headers --- */
    char *cursor = line_end + 2;
    while (cursor < buf + header_len - 2) {
        char *next = strstr(cursor, "\r\n");
        if (!next || next > buf + header_len) break;
        *next = '\0';
        if (*cursor == '\0') { cursor = next + 2; break; }

        char *colon = strchr(cursor, ':');
        if (colon && req->header_count < HTTP_MAX_HEADERS) {
            *colon = '\0';
            char *name = trim(cursor);
            char *val = trim(colon + 1);
            snprintf(req->headers[req->header_count].name,
                     sizeof(req->headers[0].name), "%s", name);
            snprintf(req->headers[req->header_count].value,
                     sizeof(req->headers[0].value), "%s", val);
            req->header_count++;
        }
        cursor = next + 2;
    }

    extract_session_cookie(req);

    /* --- body --- */
    long content_length = find_content_length(req);
    if (content_length < 0) content_length = 0;
    if ((size_t)content_length > HTTP_MAX_BODY - 1) content_length = HTTP_MAX_BODY - 1;

    size_t already = len - header_len; /* body bytes already read into buf */
    size_t to_copy = already < (size_t)content_length ? already : (size_t)content_length;
    memcpy(req->body, buf + header_len, to_copy);
    req->body_len = to_copy;

    while (req->body_len < (size_t)content_length) {
        ssize_t n = recv(fd, req->body + req->body_len,
                          (size_t)content_length - req->body_len, 0);
        if (n <= 0) break;
        req->body_len += (size_t)n;
    }
    req->body[req->body_len] = '\0';

    free(buf);
    return 0;
}

static const char *status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        default:  return "Unknown";
    }
}

static void send_response(int fd, const http_response_t *res) {
    char header[1024];
    int hn = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\n"
        "%s%s%s"
        "\r\n",
        res->status, status_text(res->status), res->content_type, res->body_len,
        res->set_cookie[0] ? "Set-Cookie: " : "",
        res->set_cookie[0] ? res->set_cookie : "",
        res->set_cookie[0] ? "\r\n" : "");

    send(fd, header, (size_t)hn, 0);
    if (res->body_len > 0) send(fd, res->body, res->body_len, 0);
}

static void *handle_connection(void *arg) {
    conn_ctx_t *ctx = (conn_ctx_t *)arg;
    int fd = ctx->fd;

    http_request_t req;
    if (read_request(fd, &req) == 0) {
        http_response_t res;
        memset(&res, 0, sizeof(res));
        res.status = 500;
        snprintf(res.content_type, sizeof(res.content_type), "text/plain");

        ctx->dispatch(ctx->cfg, &req, &res);

        if (!res.body) {
            http_respond_json_error(&res, 500, "no response body produced");
        }
        send_response(fd, &res);
        free(res.body);
    }

    close(fd);
    free(ctx);
    return NULL;
}

int http_server_run(const twm_config_t *cfg, http_dispatch_fn dispatch) {
    signal(SIGPIPE, SIG_IGN); /* client disconnects shouldn't kill worker threads */

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { util_log(LOG_ERROR, "socket() failed"); return -1; }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)cfg->port);

    if (strcmp(cfg->bind_addr, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, cfg->bind_addr, &addr.sin_addr) != 1) {
        util_log(LOG_ERROR, "invalid bind_addr '%s'", cfg->bind_addr);
        close(listen_fd);
        return -1;
    }

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        util_log(LOG_ERROR, "bind() failed on %s:%d (errno=%d)", cfg->bind_addr, cfg->port, errno);
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, 64) < 0) {
        util_log(LOG_ERROR, "listen() failed");
        close(listen_fd);
        return -1;
    }

    util_log(LOG_INFO, "TOMOYO-Web-Manager listening on %s:%d", cfg->bind_addr, cfg->port);

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (fd < 0) continue;

        conn_ctx_t *ctx = xmalloc(sizeof(conn_ctx_t));
        ctx->fd = fd;
        ctx->cfg = cfg;
        ctx->dispatch = dispatch;

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_connection, ctx) != 0) {
            close(fd);
            free(ctx);
            continue;
        }
        pthread_detach(tid);
    }

    close(listen_fd); /* unreachable, kept for completeness */
    return 0;
}
