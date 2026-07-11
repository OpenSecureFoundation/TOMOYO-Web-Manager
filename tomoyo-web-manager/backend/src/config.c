#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_defaults(twm_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->port = 8088;
    snprintf(cfg->bind_addr, sizeof(cfg->bind_addr), "127.0.0.1");
    snprintf(cfg->static_root, sizeof(cfg->static_root), "./frontend");
    snprintf(cfg->data_dir, sizeof(cfg->data_dir), "./data");
    snprintf(cfg->tomoyo_root, sizeof(cfg->tomoyo_root), "/sys/kernel/security/tomoyo");
    snprintf(cfg->admin_user, sizeof(cfg->admin_user), "admin");
    /* sha256("changeme") — MUST be changed via config for real deployments. */
    snprintf(cfg->admin_pass_sha256, sizeof(cfg->admin_pass_sha256),
             "057ba03d6c44104863dc7361fe4578965d1887360f90a0895882e58a6248fc86");
    cfg->session_ttl_seconds = 3600;
    cfg->profile_learning = 1;
    cfg->profile_enforcing = 3;
    cfg->cookie_secure = 0;
    cfg->allowlist_count = 0;
}

int config_load(const char *path, twm_config_t *cfg) {
    size_t len;
    char *data = read_file_all(path, &len);
    if (!data) {
        util_log(LOG_WARN, "config file '%s' not found, using defaults", path);
        return -1;
    }

    char *line = data;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *l = trim(line);
        if (*l && *l != '#') {
            char *eq = strchr(l, '=');
            if (eq) {
                *eq = '\0';
                char *key = trim(l);
                char *val = trim(eq + 1);

                if (strcmp(key, "port") == 0) cfg->port = atoi(val);
                else if (strcmp(key, "bind_addr") == 0)
                    snprintf(cfg->bind_addr, sizeof(cfg->bind_addr), "%s", val);
                else if (strcmp(key, "static_root") == 0)
                    snprintf(cfg->static_root, sizeof(cfg->static_root), "%s", val);
                else if (strcmp(key, "data_dir") == 0)
                    snprintf(cfg->data_dir, sizeof(cfg->data_dir), "%s", val);
                else if (strcmp(key, "tomoyo_root") == 0)
                    snprintf(cfg->tomoyo_root, sizeof(cfg->tomoyo_root), "%s", val);
                else if (strcmp(key, "admin_user") == 0)
                    snprintf(cfg->admin_user, sizeof(cfg->admin_user), "%s", val);
                else if (strcmp(key, "admin_pass_sha256") == 0)
                    snprintf(cfg->admin_pass_sha256, sizeof(cfg->admin_pass_sha256), "%s", val);
                else if (strcmp(key, "session_ttl_seconds") == 0)
                    cfg->session_ttl_seconds = atoi(val);
                else if (strcmp(key, "profile_learning") == 0)
                    cfg->profile_learning = atoi(val);
                else if (strcmp(key, "profile_enforcing") == 0)
                    cfg->profile_enforcing = atoi(val);
                else if (strcmp(key, "cookie_secure") == 0)
                    cfg->cookie_secure = atoi(val);
                else if (strcmp(key, "allow_exec") == 0) {
                    if (cfg->allowlist_count < TWM_MAX_ALLOWLIST) {
                        snprintf(cfg->allowlist[cfg->allowlist_count],
                                 sizeof(cfg->allowlist[0]), "%s", val);
                        cfg->allowlist_count++;
                    }
                } else {
                    util_log(LOG_WARN, "config: unknown key '%s'", key);
                }
            }
        }
        line = nl ? nl + 1 : NULL;
    }

    free(data);
    return 0;
}
