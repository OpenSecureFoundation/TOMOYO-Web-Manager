#ifndef TWM_UTIL_H
#define TWM_UTIL_H

#include <stddef.h>

/* Simple leveled logger. Writes to stdout (captured by systemd/docker logs). */
typedef enum { LOG_DEBUG = 0, LOG_INFO, LOG_WARN, LOG_ERROR } log_level_t;

void util_log(log_level_t level, const char *fmt, ...);

/* Duplicate a string (portable strdup wrapper, always non-NULL or aborts). */
char *xstrdup(const char *s);

/* Safe malloc wrapper: logs and exits on OOM (server is small, fail-fast). */
void *xmalloc(size_t size);

/* Read an entire file into a heap buffer. Returns NULL on failure.
 * *out_len receives the number of bytes read (excludes the extra NUL). */
char *read_file_all(const char *path, size_t *out_len);

/* Write (overwrite) a whole buffer to a file. Returns 0 on success. */
int write_file_all(const char *path, const char *data, size_t len);

/* Append a buffer to a file, creating it if needed. Returns 0 on success. */
int append_file_all(const char *path, const char *data, size_t len);

/* URL-decode src into dst (dst must be at least strlen(src)+1 bytes). */
void url_decode(const char *src, char *dst);

/* Minimal JSON string escaping into a caller-provided buffer. */
void json_escape(const char *in, char *out, size_t out_size);

/* Generate a random hex token of given byte length (2*len hex chars + NUL). */
void random_hex_token(char *out, size_t byte_len);

/* Current time as ISO-8601 UTC string into buf (buf >= 32 bytes). */
void iso_now(char *buf, size_t buf_size);

/* Trim leading/trailing whitespace in place, returns pointer within s. */
char *trim(char *s);

#endif
