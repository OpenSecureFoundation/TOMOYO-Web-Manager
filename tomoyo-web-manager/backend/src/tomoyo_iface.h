#ifndef TWM_TOMOYO_IFACE_H
#define TWM_TOMOYO_IFACE_H

#include "config.h"
#include <stddef.h>

#define TWM_MAX_DOMAINS   256
#define TWM_MAX_RULES     512
#define TWM_DOMAIN_NAME_LEN 512
#define TWM_RULE_LEN      512

typedef enum { TWM_MODE_UNKNOWN = 0, TWM_MODE_LEARNING, TWM_MODE_ENFORCING } twm_mode_t;

typedef struct {
    char name[TWM_DOMAIN_NAME_LEN];
    int profile;
    twm_mode_t mode;
} twm_domain_t;

typedef struct {
    twm_domain_t items[TWM_MAX_DOMAINS];
    int count;
} twm_domain_list_t;

typedef struct {
    char lines[TWM_MAX_RULES][TWM_RULE_LEN];
    int count;
} twm_rule_list_t;

/* Initializes the interface: detects whether the real TOMOYO securityfs
 * tree is present at cfg->tomoyo_root; if not, falls back to a local
 * "mock" tree under <data_dir>/mock_tomoyo so the app remains fully
 * demoable (per the project's dashboard/demo requirement) without a
 * TOMOYO-enabled kernel. Returns 1 if running against the real kernel
 * interface, 0 if running in mock mode. */
int tomoyo_iface_init(const twm_config_t *cfg);

/* True if operating against the real kernel securityfs tree. */
int tomoyo_iface_is_real(void);

/* Parses domain_policy and lists all domains with resolved mode. */
int tomoyo_list_domains(twm_domain_list_t *out);

/* Lists the ACL rule lines for a given domain (excludes the "<domain>"
 * header line and the "use_profile N" line). */
int tomoyo_list_rules(const char *domain, twm_rule_list_t *out);

/* Switches a domain's profile to the one matching the requested mode.
 * Returns 0 on success. Also appends an entry to the mode-change
 * history log (data_dir/mode_history.log). */
int tomoyo_set_mode(const char *domain, twm_mode_t mode, const char *changed_by);

/* Validates the syntax of a single TOMOYO ACL rule line.
 * Returns 1 if it looks well-formed, 0 otherwise; on failure, writes a
 * human-readable reason into err (err_size bytes). This is a first-line
 * check — the kernel remains authoritative once applied. */
int tomoyo_validate_rule(const char *rule, char *err, size_t err_size);

/* Applies (adds) a validated rule to a domain. Snapshots the previous
 * domain_policy state first so tomoyo_rollback() can restore it.
 * Returns 0 on success. */
int tomoyo_apply_rule(const char *domain, const char *rule, const char *changed_by);

/* Deletes a rule from a domain (exact line match). Snapshots first. */
int tomoyo_delete_rule(const char *domain, const char *rule, const char *changed_by);

/* Restores the most recent domain_policy snapshot (undo last apply/delete). */
int tomoyo_rollback(char *err, size_t err_size);

/* Reads TOMOYO stats (/sys/kernel/security/tomoyo/stat) as raw text into
 * a heap buffer (caller frees). */
char *tomoyo_read_stat(void);

/* Reads the audit/violation log, optionally filtering to only "denied"
 * entries when violations_only != 0. Returns a heap buffer of JSON-ready
 * plain lines (caller frees), newest first, capped to max_lines. */
char *tomoyo_read_logs(int violations_only, int max_lines);

/* --- Learning-capture / auto-generation workflow --- */

/* Starts a learning-capture session for a domain matching the given
 * executable path: switches the domain to learning mode, then launches
 * the executable (must be present in the config allow-list) so its
 * accesses get recorded by the kernel. Returns 0 on success and fills
 * child_pid with the spawned process id (the caller/UI is expected to
 * let the workload run, then call tomoyo_list_rules() to fetch what was
 * learned, and finally switch to enforcing mode once satisfied). */
int tomoyo_start_learning_capture(const twm_config_t *cfg, const char *exec_path,
                                   char *domain_out, size_t domain_out_size,
                                   int *child_pid, char *err, size_t err_size);

/* Exports the current rule set for a domain as a downloadable .policy
 * text blob (heap buffer, caller frees). */
char *tomoyo_export_policy(const char *domain);

#endif
