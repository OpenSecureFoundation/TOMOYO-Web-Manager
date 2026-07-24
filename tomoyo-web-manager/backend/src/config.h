#ifndef TWM_CONFIG_H
#define TWM_CONFIG_H

#define TWM_MAX_ALLOWLIST 32

typedef struct {
    int port;
    char bind_addr[64];
    char static_root[256];        /* frontend files */
    char data_dir[256];           /* app data: sessions/history/backups */

    /* TOMOYO securityfs interface paths (overridable for dev/mock mode) */
    char tomoyo_root[256];        /* e.g. /sys/kernel/security/tomoyo */

    /* Admin credentials */
    char admin_user[64];
    char admin_pass_sha256[65];   /* hex-encoded SHA-256, 64 chars + NUL */

    /* Session */
    int session_ttl_seconds;

    /* TOMOYO profile numbers used for the Learning / Enforcing toggle.
     * Auto-detected at startup by scanning the profile file for the
     * words "learning"/"enforcing"; these are the fallback defaults
     * when auto-detection fails (kept overridable for non-standard
     * profile setups). */
    int profile_learning;
    int profile_enforcing;

    /* Set to 1 when the app sits behind an HTTPS-terminating reverse
     * proxy (recommended production setup) so the session cookie gets
     * the "Secure" attribute. Leave 0 for local HTTP development. */
    int cookie_secure;

    /* Allow-list of executable paths for the auto-generation ("learning
     * capture") feature. Executing arbitrary attacker-supplied paths from
     * a privileged web endpoint would be dangerous, so only paths listed
     * here may be triggered by the "generate policy" API. */
    char allowlist[TWM_MAX_ALLOWLIST][256];
    int allowlist_count;
} twm_config_t;

/* Loads key=value config file. Returns 0 on success. Unset keys keep
 * the sane defaults already present in *cfg when called. */
int config_load(const char *path, twm_config_t *cfg);

/* Fill cfg with built-in defaults (dev-friendly, mock paths). */
void config_defaults(twm_config_t *cfg);

#endif
