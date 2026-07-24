#ifndef TWM_SHA256_H
#define TWM_SHA256_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    unsigned char buffer[64];
    size_t buffer_len;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const unsigned char *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, unsigned char out[32]);

/* Convenience: SHA-256 of a NUL-terminated string, hex-encoded into
 * out (must be >= 65 bytes). */
void sha256_hex_string(const char *input, char *out);

#endif
