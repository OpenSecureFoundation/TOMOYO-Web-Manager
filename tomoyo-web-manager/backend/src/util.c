#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>
#include <pthread.h>

static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_name(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO";
        case LOG_WARN:  return "WARN";
        case LOG_ERROR: return "ERROR";
        default:        return "?";
    }
}

void util_log(log_level_t level, const char *fmt, ...) {
    char ts[32];
    iso_now(ts, sizeof(ts));

    pthread_mutex_lock(&g_log_mutex);
    fprintf(stdout, "[%s] [%s] ", ts, level_name(level));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
    fflush(stdout);
    pthread_mutex_unlock(&g_log_mutex);
}

char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p) {
        util_log(LOG_ERROR, "out of memory (requested %zu bytes)", size);
        exit(1);
    }
    return p;
}

char *read_file_all(const char *path, size_t *out_len) {
    /* Kernel securityfs pseudo-files do not report a size via stat(),
     * so we read in growing chunks instead of relying on st_size. */
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;

    size_t cap = 8192, len = 0;
    char *buf = xmalloc(cap);

    for (;;) {
        if (len + 4096 > cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); close(fd); return NULL; }
            buf = nb;
        }
        ssize_t n = read(fd, buf + len, cap - len - 1);
        if (n < 0) { free(buf); close(fd); return NULL; }
        if (n == 0) break;
        len += (size_t)n;
    }
    close(fd);
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

int write_file_all(const char *path, const char *data, size_t len) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0640);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n < 0) { close(fd); return -1; }
        off += (size_t)n;
    }
    close(fd);
    return 0;
}

int append_file_all(const char *path, const char *data, size_t len) {
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0640);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n < 0) { close(fd); return -1; }
        off += (size_t)n;
    }
    close(fd);
    return 0;
}

void url_decode(const char *src, char *dst) {
    char a, b;
    while (*src) {
        if (*src == '%' && (a = src[1]) && (b = src[2]) &&
            isxdigit((unsigned char)a) && isxdigit((unsigned char)b)) {
            a = (char)tolower((unsigned char)a);
            b = (char)tolower((unsigned char)b);
            a = (char)(a >= 'a' ? a - 'a' + 10 : a - '0');
            b = (char)(b >= 'a' ? b - 'a' + 10 : b - '0');
            *dst++ = (char)(16 * a + b);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

void json_escape(const char *in, char *out, size_t out_size) {
    size_t o = 0;
    for (; *in && o + 2 < out_size; in++) {
        unsigned char c = (unsigned char)*in;
        switch (c) {
            case '"':  out[o++] = '\\'; out[o++] = '"'; break;
            case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
            case '\n': out[o++] = '\\'; out[o++] = 'n'; break;
            case '\r': out[o++] = '\\'; out[o++] = 'r'; break;
            case '\t': out[o++] = '\\'; out[o++] = 't'; break;
            default:
                if (c < 0x20) {
                    if (o + 6 < out_size) {
                        snprintf(out + o, 7, "\\u%04x", c);
                        o += 6;
                    }
                } else {
                    out[o++] = (char)c;
                }
        }
    }
    out[o] = '\0';
}

void random_hex_token(char *out, size_t byte_len) {
    unsigned char raw[64];
    if (byte_len > sizeof(raw)) byte_len = sizeof(raw);
    size_t remaining = byte_len;

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        size_t off = 0;
        while (remaining > 0) {
            size_t want = remaining;
            if (want > sizeof(raw)) want = sizeof(raw); /* bounds hint for the compiler */
            ssize_t n = read(fd, raw + off, want);
            if (n <= 0) break;
            off += (size_t)n;
            remaining -= (size_t)n;
        }
        close(fd);
    } else {
        /* Extremely unlikely fallback; still seeded per-process. */
        for (size_t i = 0; i < byte_len; i++) raw[i] = (unsigned char)rand();
    }

    static const char hexd[] = "0123456789abcdef";
    for (size_t i = 0; i < byte_len; i++) {
        out[i * 2]     = hexd[(raw[i] >> 4) & 0xF];
        out[i * 2 + 1] = hexd[raw[i] & 0xF];
    }
    out[byte_len * 2] = '\0';
}

void iso_now(char *buf, size_t buf_size) {
    time_t t = time(NULL);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}
