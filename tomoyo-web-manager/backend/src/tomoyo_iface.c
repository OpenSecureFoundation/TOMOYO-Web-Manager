#include "tomoyo_iface.h"
#include "util.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>

static twm_config_t g_cfg;
static char g_root[320];
static int g_is_real = 0;
static pthread_mutex_t g_write_mutex = PTHREAD_MUTEX_INITIALIZER; /* serialize policy writes */

/* ---- paths ---- */

static void path_of(char *out, size_t out_size, const char *leaf) {
    snprintf(out, out_size, "%s/%s", g_root, leaf);
}

static void backup_path(char *out, size_t out_size) {
    snprintf(out, out_size, "%s/domain_policy.bak", g_cfg.data_dir);
}

static void history_path(char *out, size_t out_size) {
    snprintf(out, out_size, "%s/mode_history.log", g_cfg.data_dir);
}

static void audit_path(char *out, size_t out_size) {
    path_of(out, out_size, "audit");
}

/* ---- mock-mode bootstrap (used when the real kernel tree is absent,
 * e.g. developing on a machine without TOMOYO enabled) ---- */

static void ensure_dir(const char *path) {
    mkdir(path, 0750);
}

static void seed_mock_tree(void) {
    ensure_dir(g_root);

    char p[512];

    path_of(p, sizeof(p), "profile");
    if (access(p, F_OK) != 0) {
        const char *content =
            "0-COMMENT=Disabled mode\n"
            "0-CONFIG={ mode=disabled }\n"
            "1-COMMENT=Learning mode\n"
            "1-CONFIG={ mode=learning }\n"
            "2-COMMENT=Permissive mode\n"
            "2-CONFIG={ mode=permissive }\n"
            "3-COMMENT=Enforcing mode\n"
            "3-CONFIG={ mode=enforcing }\n";
        write_file_all(p, content, strlen(content));
    }

    path_of(p, sizeof(p), "domain_policy");
    if (access(p, F_OK) != 0) {
        const char *content =
            "<kernel>\n"
            "use_profile 0\n"
            "\n"
            "<kernel> /usr/sbin/sshd\n"
            "use_profile 1\n"
            "file read /etc/ssh/sshd_config\n"
            "file read /usr/sbin/sshd\n"
            "file execute /usr/sbin/sshd\n"
            "network inet stream bind 0.0.0.0 22\n"
            "\n"
            "<kernel> /usr/sbin/sshd /bin/bash\n"
            "use_profile 3\n"
            "file read /bin/bash\n"
            "file execute /bin/bash\n"
            "file read /etc/passwd\n";
        write_file_all(p, content, strlen(content));
    }

    path_of(p, sizeof(p), "stat");
    if (access(p, F_OK) != 0) {
        const char *content =
            "policy_memory_used: 4096\n"
            "policy_memory_quota: 1048576\n"
            "domain_count: 3\n";
        write_file_all(p, content, strlen(content));
    }

    path_of(p, sizeof(p), "audit");
    if (access(p, F_OK) != 0) {
        time_t now = time(NULL);
        char ts[32];
        struct tm tmv;
        gmtime_r(&now, &tmv);
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tmv);
        char content[512];
        snprintf(content, sizeof(content),
                 "%s granted domain=\"<kernel> /usr/sbin/sshd\" file read /etc/ssh/sshd_config\n"
                 "%s denied  domain=\"<kernel> /usr/sbin/sshd\" file write /etc/shadow\n",
                 ts, ts);
        write_file_all(p, content, strlen(content));
    }
}

/* ---- init ---- */

int tomoyo_iface_init(const twm_config_t *cfg) {
    g_cfg = *cfg;

    char probe[400];
    snprintf(probe, sizeof(probe), "%s/domain_policy", cfg->tomoyo_root);
    if (access(cfg->tomoyo_root, F_OK) == 0 && access(probe, R_OK) == 0) {
        snprintf(g_root, sizeof(g_root), "%s", cfg->tomoyo_root);
        g_is_real = 1;
        util_log(LOG_INFO, "TOMOYO securityfs detected at %s (real mode)", g_root);
    } else {
        snprintf(g_root, sizeof(g_root), "%s/mock_tomoyo", cfg->data_dir);
        g_is_real = 0;
        ensure_dir(cfg->data_dir);
        seed_mock_tree();
        util_log(LOG_WARN,
                 "TOMOYO securityfs not found at '%s'; running in MOCK MODE using '%s'. "
                 "See docs/DEPLOIEMENT.md to enable the real kernel module.",
                 cfg->tomoyo_root, g_root);
    }

    /* Auto-detect learning/enforcing profile numbers from the profile file. */
    char pp[400];
    path_of(pp, sizeof(pp), "profile");
    size_t plen;
    char *pdata = read_file_all(pp, &plen);
    if (pdata) {
        char *line = pdata;
        while (line && *line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            int num;
            if (sscanf(line, "%d-", &num) == 1) {
                char lower[256];
                size_t i = 0;
                for (; line[i] && i < sizeof(lower) - 1; i++)
                    lower[i] = (char)tolower((unsigned char)line[i]);
                lower[i] = '\0';
                if (strstr(lower, "learning")) g_cfg.profile_learning = num;
                if (strstr(lower, "enforcing")) g_cfg.profile_enforcing = num;
            }
            line = nl ? nl + 1 : NULL;
        }
        free(pdata);
    }
    util_log(LOG_INFO, "profile mapping: learning=%d enforcing=%d",
             g_cfg.profile_learning, g_cfg.profile_enforcing);

    ensure_dir(g_cfg.data_dir);
    return g_is_real;
}

int tomoyo_iface_is_real(void) {
    return g_is_real;
}

/* ---- domain_policy parsing ---- */

static twm_mode_t mode_from_profile(int profile) {
    if (profile == g_cfg.profile_learning) return TWM_MODE_LEARNING;
    if (profile == g_cfg.profile_enforcing) return TWM_MODE_ENFORCING;
    return TWM_MODE_UNKNOWN;
}

int tomoyo_list_domains(twm_domain_list_t *out) {
    out->count = 0;
    char p[400];
    path_of(p, sizeof(p), "domain_policy");
    size_t len;
    char *data = read_file_all(p, &len);
    if (!data) return -1;

    twm_domain_t *cur = NULL;
    char *line = data;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (line[0] == '<') {
            if (out->count < TWM_MAX_DOMAINS) {
                cur = &out->items[out->count++];
                snprintf(cur->name, sizeof(cur->name), "%s", line);
                cur->profile = -1;
                cur->mode = TWM_MODE_UNKNOWN;
            } else {
                cur = NULL;
            }
        } else if (cur && strncmp(line, "use_profile ", 12) == 0) {
            cur->profile = atoi(line + 12);
            cur->mode = mode_from_profile(cur->profile);
        }
        /* other lines (ACL rules, blank separators) are skipped here;
         * see tomoyo_list_rules() for per-domain rule extraction. */

        line = nl ? nl + 1 : NULL;
    }

    free(data);
    return 0;
}

int tomoyo_list_rules(const char *domain, twm_rule_list_t *out) {
    out->count = 0;
    char p[400];
    path_of(p, sizeof(p), "domain_policy");
    size_t len;
    char *data = read_file_all(p, &len);
    if (!data) return -1;

    int in_target = 0;
    char *line = data;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (line[0] == '<') {
            in_target = (strcmp(line, domain) == 0);
        } else if (in_target && line[0] != '\0' &&
                   strncmp(line, "use_profile ", 12) != 0) {
            if (out->count < TWM_MAX_RULES) {
                snprintf(out->lines[out->count], sizeof(out->lines[0]), "%s", line);
                out->count++;
            }
        }

        line = nl ? nl + 1 : NULL;
    }

    free(data);
    return 0;
}

/* ---- mode toggle ---- */

static void log_history(const char *action, const char *domain, const char *by,
                         const char *detail) {
    char ts[32];
    iso_now(ts, sizeof(ts));
    char line[1024];
    int n = snprintf(line, sizeof(line), "%s\t%s\t%s\t%s\t%s\n",
                      ts, action, domain ? domain : "-", by ? by : "-",
                      detail ? detail : "");
    char hp[300];
    history_path(hp, sizeof(hp));
    append_file_all(hp, line, (size_t)n);
}

/* ---- mock-mode stateful command simulator ----
 * On a real kernel, writing "select domain=\"X\"\n<command>\n" to
 * domain_policy is interpreted statefully by kernel code: the change is
 * applied directly to domain X's live in-kernel structure and is
 * correctly reflected on every subsequent read, no matter where in the
 * file domain X's text happened to be. Our local mock tree is just a
 * flat text file with no engine behind it, so naively *appending* the
 * same command (as we do against the real kernel) would not actually
 * move the "use_profile" line or insert the ACL line into domain X's
 * block. This function reproduces the kernel's effective behaviour by
 * rewriting the correct section of the mock file in place, keeping the
 * demo/mock mode faithful to how the real deployment behaves. */
static char *mock_apply_command(const char *original, const char *domain, const char *command) {
    int is_use_profile = strncmp(command, "use_profile ", 12) == 0;
    int is_delete = strncmp(command, "delete ", 7) == 0;
    const char *delete_target = is_delete ? command + 7 : NULL;

    json_buf_t jb;
    jb_init(&jb);

    char *copy = xstrdup(original);
    char *line = copy;
    int in_target = 0;
    int found_domain = 0;
    int profile_written = 0;
    int rule_written = 0;

    while (line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        int is_header = (line[0] == '<');

        if (in_target && (is_header || line[0] == '\0')) {
            /* Leaving the target domain's block: flush a pending
             * use_profile/new-rule line before the boundary line. */
            if (is_use_profile && !profile_written) {
                jb_raw(&jb, command); jb_char(&jb, '\n');
                profile_written = 1;
            } else if (!is_use_profile && !is_delete && !rule_written) {
                jb_raw(&jb, command); jb_char(&jb, '\n');
                rule_written = 1;
            }
            in_target = 0;
        }

        if (is_header) {
            in_target = (strcmp(line, domain) == 0);
            if (in_target) found_domain = 1;
        }

        int skip_line = 0;
        if (in_target) {
            if (is_use_profile && strncmp(line, "use_profile ", 12) == 0) {
                jb_raw(&jb, command); jb_char(&jb, '\n');
                profile_written = 1;
                skip_line = 1;
            } else if (is_delete && delete_target && strcmp(line, delete_target) == 0) {
                skip_line = 1;
            }
        }

        if (!skip_line) {
            jb_raw(&jb, line);
            jb_char(&jb, '\n');
        }

        line = nl ? nl + 1 : NULL;
    }
    free(copy);

    if (in_target) { /* target domain's block ran to EOF */
        if (is_use_profile && !profile_written) { jb_raw(&jb, command); jb_char(&jb, '\n'); }
        else if (!is_use_profile && !is_delete && !rule_written) {
            jb_raw(&jb, command); jb_char(&jb, '\n');
        }
    }

    if (!found_domain && !is_delete) {
        /* Domain not seen yet (e.g. a binary never executed before):
         * create a fresh block so the mock stays usable end-to-end. */
        jb_char(&jb, '\n');
        jb_raw(&jb, domain); jb_char(&jb, '\n');
        if (is_use_profile) {
            jb_raw(&jb, command); jb_char(&jb, '\n');
        } else {
            jb_raw(&jb, "use_profile 0\n");
            jb_raw(&jb, command); jb_char(&jb, '\n');
        }
    }

    return jb.buf; /* ownership transferred to caller (free() it) */
}

/* Applies one policy command line to `domain`, the same way whether we
 * are backed by a real kernel (plain append; the kernel does the rest)
 * or by the local mock tree (rewritten in place — see above). */
static int write_policy_command(const char *domain, const char *command) {
    char p[400];
    path_of(p, sizeof(p), "domain_policy");

    int rc;
    if (g_is_real) {
        char payload[TWM_DOMAIN_NAME_LEN + TWM_RULE_LEN + 64];
        int n = snprintf(payload, sizeof(payload), "select domain=\"%s\"\n%s\n", domain, command);
        rc = append_file_all(p, payload, (size_t)n);
    } else {
        size_t len;
        char *data = read_file_all(p, &len);
        if (!data) return -1;
        char *updated = mock_apply_command(data, domain, command);
        free(data);
        rc = write_file_all(p, updated, strlen(updated));
        free(updated);
    }
    return rc;
}

int tomoyo_set_mode(const char *domain, twm_mode_t mode, const char *changed_by) {
    int profile = (mode == TWM_MODE_LEARNING) ? g_cfg.profile_learning
                : (mode == TWM_MODE_ENFORCING) ? g_cfg.profile_enforcing
                : -1;
    if (profile < 0) return -1;

    char command[64];
    snprintf(command, sizeof(command), "use_profile %d", profile);

    pthread_mutex_lock(&g_write_mutex);
    int rc = write_policy_command(domain, command);
    pthread_mutex_unlock(&g_write_mutex);

    if (rc == 0) {
        log_history(mode == TWM_MODE_LEARNING ? "mode->learning" : "mode->enforcing",
                    domain, changed_by, NULL);
    }
    return rc;
}

/* ---- rule syntax validation ----
 * First-line check for the most common TOMOYO ACL directive families.
 * The kernel remains the final authority once the rule is applied. */

static const char *FILE_PERMS[] = {
    "read", "write", "append", "create", "unlink", "getattr", "mkdir", "rmdir",
    "mkfifo", "mkblock", "mkchar", "mksock", "truncate", "symlink", "rename",
    "link", "execute", "chmod", "chown", "chgrp", "ioctl", "chroot", "mount",
    "umount", "pivot_root", NULL
};

static int is_known_file_perm(const char *tok) {
    for (int i = 0; FILE_PERMS[i]; i++)
        if (strcmp(tok, FILE_PERMS[i]) == 0) return 1;
    return 0;
}

int tomoyo_validate_rule(const char *rule, char *err, size_t err_size) {
    char buf[TWM_RULE_LEN];
    snprintf(buf, sizeof(buf), "%s", rule);
    char *s = trim(buf);

    if (*s == '\0') {
        snprintf(err, err_size, "La regle est vide.");
        return 0;
    }
    /* allow explicit deletion prefix, validated the same as the base rule */
    if (strncmp(s, "delete ", 7) == 0) s += 7;

    char directive[32] = {0};
    sscanf(s, "%31s", directive);

    if (strcmp(directive, "file") == 0) {
        char perm[32] = {0}, path[TWM_RULE_LEN] = {0};
        int matched = sscanf(s, "file %31s %511[^\n]", perm, path);
        if (matched < 2) {
            snprintf(err, err_size,
                     "Syntaxe attendue: 'file <permission> <chemin>'.");
            return 0;
        }
        if (!is_known_file_perm(perm)) {
            snprintf(err, err_size, "Permission fichier inconnue: '%s'.", perm);
            return 0;
        }
        if (path[0] != '/' && path[0] != '{' && strcmp(path, "any") != 0) {
            snprintf(err, err_size,
                     "Le chemin doit etre absolu (commencer par '/'): '%s'.", path);
            return 0;
        }
        return 1;
    }

    if (strcmp(directive, "network") == 0) {
        char family[16] = {0}, proto[16] = {0}, op[16] = {0}, addr[TWM_RULE_LEN] = {0};
        int matched = sscanf(s, "network %15s %15s %15s %511[^\n]", family, proto, op, addr);
        if (matched < 4) {
            snprintf(err, err_size,
                     "Syntaxe attendue: 'network <inet|unix> <proto> <bind|listen|connect|accept> <adresse>'.");
            return 0;
        }
        if (strcmp(family, "inet") != 0 && strcmp(family, "unix") != 0) {
            snprintf(err, err_size, "Famille reseau inconnue: '%s' (attendu inet/unix).", family);
            return 0;
        }
        return 1;
    }

    if (strcmp(directive, "misc") == 0 || strcmp(directive, "capability") == 0 ||
        strcmp(directive, "ipc") == 0   || strcmp(directive, "task") == 0) {
        /* Accept: kernel is authoritative on the fine-grained sub-syntax
         * of these less common directive families. */
        return 1;
    }

    snprintf(err, err_size,
             "Directive inconnue: '%s' (attendu file/network/misc/capability/ipc/task).",
             directive);
    return 0;
}

/* ---- snapshot / rollback ---- */

static int snapshot_domain_policy(void) {
    char p[400], b[400];
    path_of(p, sizeof(p), "domain_policy");
    backup_path(b, sizeof(b));

    size_t len;
    char *data = read_file_all(p, &len);
    if (!data) return -1;
    int rc = write_file_all(b, data, len);
    free(data);
    return rc;
}

int tomoyo_rollback(char *err, size_t err_size) {
    char p[400], b[400];
    path_of(p, sizeof(p), "domain_policy");
    backup_path(b, sizeof(b));

    size_t len;
    char *data = read_file_all(b, &len);
    if (!data) {
        snprintf(err, err_size, "Aucune sauvegarde disponible pour un retour arriere.");
        return -1;
    }

    pthread_mutex_lock(&g_write_mutex);
    int rc = write_file_all(p, data, len);
    pthread_mutex_unlock(&g_write_mutex);
    free(data);

    if (rc != 0) snprintf(err, err_size, "Echec de l'ecriture lors du rollback.");
    return rc;
}

int tomoyo_apply_rule(const char *domain, const char *rule, const char *changed_by) {
    pthread_mutex_lock(&g_write_mutex);
    if (snapshot_domain_policy() != 0) {
        pthread_mutex_unlock(&g_write_mutex);
        return -1;
    }
    int rc = write_policy_command(domain, rule);
    pthread_mutex_unlock(&g_write_mutex);

    if (rc == 0) log_history("rule.add", domain, changed_by, rule);
    return rc;
}

int tomoyo_delete_rule(const char *domain, const char *rule, const char *changed_by) {
    char command[TWM_RULE_LEN + 8];
    snprintf(command, sizeof(command), "delete %s", rule);

    pthread_mutex_lock(&g_write_mutex);
    if (snapshot_domain_policy() != 0) {
        pthread_mutex_unlock(&g_write_mutex);
        return -1;
    }
    int rc = write_policy_command(domain, command);
    pthread_mutex_unlock(&g_write_mutex);

    if (rc == 0) log_history("rule.delete", domain, changed_by, rule);
    return rc;
}

/* ---- stats / logs ---- */

char *tomoyo_read_stat(void) {
    char p[400];
    path_of(p, sizeof(p), "stat");
    size_t len;
    char *data = read_file_all(p, &len);
    if (!data) return xstrdup("");
    return data;
}

char *tomoyo_read_logs(int violations_only, int max_lines) {
    char p[400];
    audit_path(p, sizeof(p));
    size_t len;
    char *data = read_file_all(p, &len);
    if (!data) return xstrdup("");

    /* Count lines, then walk from the end to keep only the last
     * max_lines (newest-first output), optionally filtering "denied". */
    size_t total_lines = 0;
    for (size_t i = 0; i < len; i++) if (data[i] == '\n') total_lines++;

    char **line_ptrs = xmalloc(sizeof(char *) * (total_lines + 1));
    size_t nl_count = 0;
    char *line = data;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        line_ptrs[nl_count++] = line;
        line = nl ? nl + 1 : NULL;
    }

    size_t cap = 4096, outlen = 0;
    char *out = xmalloc(cap);
    out[0] = '\0';
    int emitted = 0;

    for (long i = (long)nl_count - 1; i >= 0 && emitted < max_lines; i--) {
        const char *l = line_ptrs[i];
        if (l[0] == '\0') continue;
        if (violations_only && !strstr(l, "denied")) continue;

        size_t llen = strlen(l);
        if (outlen + llen + 2 > cap) {
            cap *= 2;
            char *nb = realloc(out, cap);
            if (!nb) break;
            out = nb;
        }
        memcpy(out + outlen, l, llen);
        outlen += llen;
        out[outlen++] = '\n';
        out[outlen] = '\0';
        emitted++;
    }

    free(line_ptrs);
    free(data);
    return out;
}

/* ---- learning capture / auto-generation ---- */

static int is_allowed_exec(const char *path) {
    for (int i = 0; i < g_cfg.allowlist_count; i++)
        if (strcmp(g_cfg.allowlist[i], path) == 0) return 1;
    return 0;
}

static void *reaper_thread(void *arg) {
    pid_t pid = (pid_t)(intptr_t)arg;
    int status;
    waitpid(pid, &status, 0);
    return NULL;
}

int tomoyo_start_learning_capture(const twm_config_t *cfg, const char *exec_path,
                                   char *domain_out, size_t domain_out_size,
                                   int *child_pid, char *err, size_t err_size) {
    (void)cfg;
    if (!is_allowed_exec(exec_path)) {
        snprintf(err, err_size,
                 "Chemin non present dans la liste blanche 'allow_exec' de la configuration.");
        return -1;
    }
    if (access(exec_path, X_OK) != 0) {
        snprintf(err, err_size, "Le fichier '%s' est introuvable ou non executable.", exec_path);
        return -1;
    }

    snprintf(domain_out, domain_out_size, "<kernel> %s", exec_path);

    /* Best-effort: pre-switch the target domain to learning mode if it
     * already exists (first run of a never-seen binary will be created
     * by the kernel itself on exec, inheriting the parent's profile). */
    tomoyo_set_mode(domain_out, TWM_MODE_LEARNING, "auto-capture");

    pid_t pid = fork();
    if (pid < 0) {
        snprintf(err, err_size, "fork() a echoue.");
        return -1;
    }
    if (pid == 0) {
        /* child */
        execl(exec_path, exec_path, (char *)NULL);
        _exit(127);
    }
    *child_pid = (int)pid;

    pthread_t tid;
    if (pthread_create(&tid, NULL, reaper_thread, (void *)(intptr_t)pid) == 0) {
        pthread_detach(tid);
    }

    log_history("learning.capture.start", domain_out, "auto-capture", exec_path);
    return 0;
}

char *tomoyo_export_policy(const char *domain) {
    twm_rule_list_t rules;
    tomoyo_list_rules(domain, &rules);

    size_t cap = 4096, len = 0;
    char *out = xmalloc(cap);
    int n = snprintf(out, cap, "%s\n", domain);
    len = (size_t)n;

    for (int i = 0; i < rules.count; i++) {
        size_t rlen = strlen(rules.lines[i]);
        if (len + rlen + 2 > cap) {
            cap *= 2;
            char *nb = realloc(out, cap);
            if (!nb) break;
            out = nb;
        }
        memcpy(out + len, rules.lines[i], rlen);
        len += rlen;
        out[len++] = '\n';
        out[len] = '\0';
    }
    return out;
}
