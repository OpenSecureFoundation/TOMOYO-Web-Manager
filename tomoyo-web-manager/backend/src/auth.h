#ifndef TWM_AUTH_H
#define TWM_AUTH_H

#include "config.h"
#include <stddef.h>

#define TWM_TOKEN_HEX_LEN 64  /* 32 bytes -> 64 hex chars */

void auth_init(const twm_config_t *cfg);

/* Computes lowercase-hex SHA-256 of `input` into out (>=65 bytes). */
void sha256_hex(const char *input, char *out);

/* Verifies username/password against configured admin account.
 * Returns 1 if valid, 0 otherwise. */
int auth_check_credentials(const char *user, const char *pass);

/* Creates a new session for the given user, returns a heap session token
 * (caller must not free — owned by the session table) or NULL on failure. */
const char *auth_create_session(const char *user);

/* Validates a session token from a cookie. Returns 1 if valid and not
 * expired (and refreshes its expiry), 0 otherwise. */
int auth_validate_session(const char *token);

/* Copies the username tied to a (valid) session token into out.
 * Returns 1 if found, 0 otherwise. */
int auth_get_user(const char *token, char *out, size_t out_size);

/* Invalidates a session (logout). */
void auth_destroy_session(const char *token);

/* Periodic cleanup of expired sessions (call from a background thread). */
void auth_reap_expired(void);

#endif
