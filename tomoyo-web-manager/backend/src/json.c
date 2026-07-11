#include "json.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#define JB_INITIAL_CAP 256

void jb_init(json_buf_t *jb) {
    jb->cap = JB_INITIAL_CAP;
    jb->len = 0;
    jb->buf = xmalloc(jb->cap);
    jb->buf[0] = '\0';
}

void jb_free(json_buf_t *jb) {
    free(jb->buf);
    jb->buf = NULL;
    jb->len = jb->cap = 0;
}

static void jb_ensure(json_buf_t *jb, size_t extra) {
    if (jb->len + extra + 1 <= jb->cap) return;
    while (jb->len + extra + 1 > jb->cap) jb->cap *= 2;
    char *nb = realloc(jb->buf, jb->cap);
    if (!nb) { util_log(LOG_ERROR, "json buffer OOM"); exit(1); }
    jb->buf = nb;
}

void jb_raw(json_buf_t *jb, const char *s) {
    size_t n = strlen(s);
    jb_ensure(jb, n);
    memcpy(jb->buf + jb->len, s, n + 1);
    jb->len += n;
}

void jb_char(json_buf_t *jb, char c) {
    jb_ensure(jb, 1);
    jb->buf[jb->len++] = c;
    jb->buf[jb->len] = '\0';
}

void jb_string(json_buf_t *jb, const char *s) {
    size_t need = strlen(s) * 2 + 3;
    jb_ensure(jb, need);
    jb->buf[jb->len++] = '"';
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  jb->buf[jb->len++] = '\\'; jb->buf[jb->len++] = '"'; break;
            case '\\': jb->buf[jb->len++] = '\\'; jb->buf[jb->len++] = '\\'; break;
            case '\n': jb->buf[jb->len++] = '\\'; jb->buf[jb->len++] = 'n'; break;
            case '\r': jb->buf[jb->len++] = '\\'; jb->buf[jb->len++] = 'r'; break;
            case '\t': jb->buf[jb->len++] = '\\'; jb->buf[jb->len++] = 't'; break;
            default:
                if (c < 0x20) {
                    jb->len += (size_t)snprintf(jb->buf + jb->len, 8, "\\u%04x", c);
                } else {
                    jb->buf[jb->len++] = (char)c;
                }
        }
    }
    jb->buf[jb->len++] = '"';
    jb->buf[jb->len] = '\0';
}

void jb_key(json_buf_t *jb, const char *key) {
    jb_string(jb, key);
    jb_char(jb, ':');
}

void jb_kv_string(json_buf_t *jb, const char *key, const char *val) {
    jb_key(jb, key);
    jb_string(jb, val);
}

void jb_kv_int(json_buf_t *jb, const char *key, long val) {
    char num[32];
    snprintf(num, sizeof(num), "%ld", val);
    jb_key(jb, key);
    jb_raw(jb, num);
}

void jb_kv_bool(json_buf_t *jb, const char *key, int val) {
    jb_key(jb, key);
    jb_raw(jb, val ? "true" : "false");
}

/* --- Minimal flat-object extractor --- */

static const char *find_field(const char *json, const char *field) {
    /* Looks for "field" followed (after whitespace) by ':'. Naive but
     * sufficient for the flat, single-level request bodies this API uses. */
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", field);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != ':') return NULL;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

int json_get_string(const char *json, const char *field, char *out, size_t out_size) {
    const char *p = find_field(json, field);
    if (!p) return 0;
    if (*p != '"') return 0;
    p++;
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < out_size) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
                case 'n': out[o++] = '\n'; break;
                case 'r': out[o++] = '\r'; break;
                case 't': out[o++] = '\t'; break;
                case '"': out[o++] = '"'; break;
                case '\\': out[o++] = '\\'; break;
                case '/': out[o++] = '/'; break;
                default: out[o++] = *p; break;
            }
            p++;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
    return 1;
}

int json_get_bool(const char *json, const char *field, int *out) {
    const char *p = find_field(json, field);
    if (!p) return 0;
    if (strncmp(p, "true", 4) == 0) { *out = 1; return 1; }
    if (strncmp(p, "false", 5) == 0) { *out = 0; return 1; }
    return 0;
}
