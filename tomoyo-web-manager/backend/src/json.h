#ifndef TWM_JSON_H
#define TWM_JSON_H
#include <stddef.h>

/* Minimal append-only JSON string builder.
 * Not a general-purpose JSON library: tailored to this API's needs
 * (flat objects/arrays of strings, ints, bools, nested objects/arrays). */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} json_buf_t;

void jb_init(json_buf_t *jb);
void jb_free(json_buf_t *jb);
void jb_raw(json_buf_t *jb, const char *s);
void jb_char(json_buf_t *jb, char c);
void jb_string(json_buf_t *jb, const char *s);   /* writes "escaped" */
void jb_key(json_buf_t *jb, const char *key);     /* writes "key": */
void jb_kv_string(json_buf_t *jb, const char *key, const char *val);
void jb_kv_int(json_buf_t *jb, const char *key, long val);
void jb_kv_bool(json_buf_t *jb, const char *key, int val);

/* Very small JSON value extractor for flat request bodies:
 * {"field": "value", "other": 123}
 * Returns 1 and copies the (already unescaped, basic-escapes-only) string
 * value into out (out_size bytes) if found, else 0. Not a full parser:
 * it does not handle nested objects/arrays as values. */
int json_get_string(const char *json, const char *field, char *out, size_t out_size);
int json_get_bool(const char *json, const char *field, int *out);

#endif
