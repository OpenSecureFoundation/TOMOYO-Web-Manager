#include "auth.h"
#include "util.h"
#include "sha256.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#define TWM_MAX_SESSIONS 64

typedef struct {
    char token[TWM_TOKEN_HEX_LEN + 1];
    char user[64];
    time_t expires_at;
    int in_use;
} session_t;

static session_t g_sessions[TWM_MAX_SESSIONS];
static pthread_mutex_t g_session_mutex = PTHREAD_MUTEX_INITIALIZER;
static twm_config_t g_cfg;

void auth_init(const twm_config_t *cfg) {
    g_cfg = *cfg;
    memset(g_sessions, 0, sizeof(g_sessions));
}

void sha256_hex(const char *input, char *out) {
    sha256_hex_string(input, out);
}

/* Constant-time comparison to avoid timing side-channels on hash compare. */
static int consttime_eq(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la != lb) return 0;
    unsigned char diff = 0;
    for (size_t i = 0; i < la; i++) diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

int auth_check_credentials(const char *user, const char *pass) {
    if (!consttime_eq(user, g_cfg.admin_user)) return 0;
    char hash[65];
    sha256_hex(pass, hash);
    return consttime_eq(hash, g_cfg.admin_pass_sha256);
}

const char *auth_create_session(const char *user) {
    pthread_mutex_lock(&g_session_mutex);
    int slot = -1;
    time_t now = time(NULL);
    for (int i = 0; i < TWM_MAX_SESSIONS; i++) {
        if (!g_sessions[i].in_use || g_sessions[i].expires_at < now) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_session_mutex);
        return NULL; /* session table full */
    }

    random_hex_token(g_sessions[slot].token, TWM_TOKEN_HEX_LEN / 2);
    snprintf(g_sessions[slot].user, sizeof(g_sessions[slot].user), "%s", user);
    g_sessions[slot].expires_at = now + g_cfg.session_ttl_seconds;
    g_sessions[slot].in_use = 1;

    const char *tok = g_sessions[slot].token;
    pthread_mutex_unlock(&g_session_mutex);
    return tok;
}

int auth_validate_session(const char *token) {
    if (!token || !*token) return 0;
    pthread_mutex_lock(&g_session_mutex);
    time_t now = time(NULL);
    int ok = 0;
    for (int i = 0; i < TWM_MAX_SESSIONS; i++) {
        if (g_sessions[i].in_use && strcmp(g_sessions[i].token, token) == 0) {
            if (g_sessions[i].expires_at >= now) {
                g_sessions[i].expires_at = now + g_cfg.session_ttl_seconds;
                ok = 1;
            }
            break;
        }
    }
    pthread_mutex_unlock(&g_session_mutex);
    return ok;
}

void auth_destroy_session(const char *token) {
    if (!token) return;
    pthread_mutex_lock(&g_session_mutex);
    for (int i = 0; i < TWM_MAX_SESSIONS; i++) {
        if (g_sessions[i].in_use && strcmp(g_sessions[i].token, token) == 0) {
            g_sessions[i].in_use = 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_session_mutex);
}

int auth_get_user(const char *token, char *out, size_t out_size) {
    if (!token || !*token) return 0;
    pthread_mutex_lock(&g_session_mutex);
    time_t now = time(NULL);
    int found = 0;
    for (int i = 0; i < TWM_MAX_SESSIONS; i++) {
        if (g_sessions[i].in_use && strcmp(g_sessions[i].token, token) == 0 &&
            g_sessions[i].expires_at >= now) {
            snprintf(out, out_size, "%s", g_sessions[i].user);
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_session_mutex);
    return found;
}

void auth_reap_expired(void) {
    pthread_mutex_lock(&g_session_mutex);
    time_t now = time(NULL);
    for (int i = 0; i < TWM_MAX_SESSIONS; i++) {
        if (g_sessions[i].in_use && g_sessions[i].expires_at < now) {
            g_sessions[i].in_use = 0;
        }
    }
    pthread_mutex_unlock(&g_session_mutex);
}
