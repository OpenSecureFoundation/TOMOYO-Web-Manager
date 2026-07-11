#include "routes.h"
#include "auth.h"
#include "tomoyo_iface.h"
#include "json.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- small helpers ---------- */

static void query_get(const char *query, const char *name, char *out, size_t out_size) {
    (void)out_size;
    out[0] = '\0';
    if (!query || !*query) return;

    size_t nlen = strlen(name);
    const char *p = query;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        size_t seglen = amp ? (size_t)(amp - p) : strlen(p);

        if (seglen > nlen && p[nlen] == '=' && strncmp(p, name, nlen) == 0) {
            char raw[HTTP_MAX_PATH_LEN];
            size_t vlen = seglen - nlen - 1;
            if (vlen >= sizeof(raw)) vlen = sizeof(raw) - 1;
            memcpy(raw, p + nlen + 1, vlen);
            raw[vlen] = '\0';
            url_decode(raw, out);
            return;
        }
        p = amp ? amp + 1 : NULL;
    }
}

static int require_auth(http_request_t *req, http_response_t *res, char *user_out, size_t user_out_size) {
    if (!auth_validate_session(req->cookie_session)) {
        http_respond_json_error(res, 401, "Authentification requise. Veuillez vous reconnecter.");
        return 0;
    }
    auth_get_user(req->cookie_session, user_out, user_out_size);
    return 1;
}

static const char *mode_to_str(twm_mode_t m) {
    switch (m) {
        case TWM_MODE_LEARNING:  return "learning";
        case TWM_MODE_ENFORCING: return "enforcing";
        default:                 return "unknown";
    }
}

/* ---------- static file serving ---------- */

static const char *content_type_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".css") == 0)  return "text/css; charset=utf-8";
    if (strcmp(dot, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (strcmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(dot, ".svg") == 0)  return "image/svg+xml";
    if (strcmp(dot, ".png") == 0)  return "image/png";
    if (strcmp(dot, ".ico") == 0)  return "image/x-icon";
    return "text/plain; charset=utf-8";
}

static void serve_static(const twm_config_t *cfg, const char *path, http_response_t *res) {
    if (strstr(path, "..")) {
        http_respond_text(res, 403, "text/plain", "Forbidden");
        return;
    }
    const char *rel = (strcmp(path, "/") == 0) ? "/index.html" : path;

    char full[600];
    snprintf(full, sizeof(full), "%s%s", cfg->static_root, rel);

    size_t len;
    char *data = read_file_all(full, &len);
    if (!data) {
        http_respond_text(res, 404, "text/plain", "Not Found");
        return;
    }
    http_respond_file(res, 200, content_type_for(full), data, len);
    free(data);
}

/* ---------- API: auth ---------- */

static void api_login(const twm_config_t *cfg, http_request_t *req, http_response_t *res) {
    char user[64] = {0}, pass[128] = {0};
    int have_user = json_get_string(req->body, "login", user, sizeof(user)) ||
                     json_get_string(req->body, "username", user, sizeof(user));
    int have_pass = json_get_string(req->body, "password", pass, sizeof(pass));

    if (!have_user || !have_pass || user[0] == '\0' || pass[0] == '\0') {
        http_respond_json_error(res, 400, "Champs mal remplis.");
        return;
    }

    if (!auth_check_credentials(user, pass)) {
        util_log(LOG_WARN, "failed login attempt for user '%s'", user);
        http_respond_json_error(res, 401, "Identifiants incorrects.");
        return;
    }

    const char *token = auth_create_session(user);
    if (!token) {
        http_respond_json_error(res, 500, "Trop de sessions actives, reessayez plus tard.");
        return;
    }

    snprintf(res->set_cookie, sizeof(res->set_cookie),
             "session=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%d%s",
             token, cfg->session_ttl_seconds, cfg->cookie_secure ? "; Secure" : "");

    json_buf_t jb; jb_init(&jb);
    jb_char(&jb, '{');
    jb_kv_string(&jb, "status", "ok");
    jb_char(&jb, ','); jb_kv_string(&jb, "user", user);
    jb_char(&jb, '}');
    http_respond_json(res, 200, jb.buf);
    jb_free(&jb);

    util_log(LOG_INFO, "user '%s' logged in", user);
}

static void api_logout(const twm_config_t *cfg, http_request_t *req, http_response_t *res) {
    auth_destroy_session(req->cookie_session);
    snprintf(res->set_cookie, sizeof(res->set_cookie),
             "session=deleted; Path=/; HttpOnly; SameSite=Strict; Max-Age=0%s",
             cfg->cookie_secure ? "; Secure" : "");
    http_respond_json(res, 200, "{\"status\":\"ok\"}");
}

static void api_session(const twm_config_t *cfg, http_request_t *req, http_response_t *res) {
    (void)cfg;
    char user[64] = {0};
    int authed = auth_validate_session(req->cookie_session);
    if (authed) auth_get_user(req->cookie_session, user, sizeof(user));

    json_buf_t jb; jb_init(&jb);
    jb_char(&jb, '{');
    jb_kv_bool(&jb, "authenticated", authed);
    jb_char(&jb, ',');
    jb_kv_string(&jb, "user", authed ? user : "");
    jb_char(&jb, ',');
    jb_kv_bool(&jb, "real_kernel_mode", tomoyo_iface_is_real());
    jb_char(&jb, '}');
    http_respond_json(res, 200, jb.buf);
    jb_free(&jb);
}

/* ---------- API: domains / rules ---------- */

static void api_domains(const twm_config_t *cfg, http_request_t *req, http_response_t *res) {
    (void)cfg;
    char user[64];
    if (!require_auth(req, res, user, sizeof(user))) return;

    twm_domain_list_t list;
    if (tomoyo_list_domains(&list) != 0) {
        http_respond_json_error(res, 500, "Impossible de lire domain_policy.");
        return;
    }

    json_buf_t jb; jb_init(&jb);
    jb_char(&jb, '{');
    jb_key(&jb, "count"); { char n[16]; snprintf(n, sizeof(n), "%d", list.count); jb_raw(&jb, n); }
    jb_char(&jb, ',');
    jb_key(&jb, "domains");
    jb_char(&jb, '[');
    for (int i = 0; i < list.count; i++) {
        if (i > 0) jb_char(&jb, ',');
        jb_char(&jb, '{');
        jb_kv_string(&jb, "name", list.items[i].name);
        jb_char(&jb, ',');
        jb_kv_int(&jb, "profile", list.items[i].profile);
        jb_char(&jb, ',');
        jb_kv_string(&jb, "mode", mode_to_str(list.items[i].mode));
        jb_char(&jb, '}');
    }
    jb_char(&jb, ']');
    if (list.count == 0) {
        jb_char(&jb, ',');
        jb_kv_string(&jb, "message", "Aucun domaine actif trouve.");
    }
    jb_char(&jb, '}');
    http_respond_json(res, 200, jb.buf);
    jb_free(&jb);
}

static void api_rules(const twm_config_t *cfg, http_request_t *req, http_response_t *res) {
    (void)cfg;
    char user[64];
    if (!require_auth(req, res, user, sizeof(user))) return;

    char domain[TWM_DOMAIN_NAME_LEN];
    query_get(req->query, "domain", domain, sizeof(domain));
    if (domain[0] == '\0') {
        http_respond_json_error(res, 400, "Parametre 'domain' manquant.");
        return;
    }

    twm_rule_list_t rules;
    tomoyo_list_rules(domain, &rules);

    json_buf_t jb; jb_init(&jb);
    jb_char(&jb, '{');
    jb_kv_string(&jb, "domain", domain);
    jb_char(&jb, ',');
    jb_key(&jb, "rules");
    jb_char(&jb, '[');
    for (int i = 0; i < rules.count; i++) {
        if (i > 0) jb_char(&jb, ',');
        jb_string(&jb, rules.lines[i]);
    }
    jb_char(&jb, ']');
    jb_char(&jb, '}');
    http_respond_json(res, 200, jb.buf);
    jb_free(&jb);
}

static void api_mode(const twm_config_t *cfg, http_request_t *req, http_response_t *res) {
    (void)cfg;
    char user[64];
    if (!require_auth(req, res, user, sizeof(user))) return;

    char domain[TWM_DOMAIN_NAME_LEN] = {0}, mode_str[32] = {0};
    json_get_string(req->body, "domain", domain, sizeof(domain));
    json_get_string(req->body, "mode", mode_str, sizeof(mode_str));

    if (domain[0] == '\0' || mode_str[0] == '\0') {
        http_respond_json_error(res, 400, "Champs 'domain'/'mode' manquants.");
        return;
    }

    twm_mode_t mode;
    if (strcmp(mode_str, "learning") == 0) mode = TWM_MODE_LEARNING;
    else if (strcmp(mode_str, "enforcing") == 0) mode = TWM_MODE_ENFORCING;
    else {
        http_respond_json_error(res, 400, "Mode invalide (attendu 'learning' ou 'enforcing').");
        return;
    }

    if (tomoyo_set_mode(domain, mode, user) != 0) {
        http_respond_json_error(res, 500, "Echec de la bascule de mode.");
        return;
    }

    http_respond_json(res, 200, "{\"status\":\"ok\"}");
}

static void api_rules_edit(const twm_config_t *cfg, http_request_t *req, http_response_t *res) {
    (void)cfg;
    char user[64];
    if (!require_auth(req, res, user, sizeof(user))) return;

    char domain[TWM_DOMAIN_NAME_LEN] = {0}, action[16] = {0}, rule[TWM_RULE_LEN] = {0};
    json_get_string(req->body, "domain", domain, sizeof(domain));
    json_get_string(req->body, "action", action, sizeof(action));
    json_get_string(req->body, "rule", rule, sizeof(rule));

    if (domain[0] == '\0' || rule[0] == '\0' ||
        (strcmp(action, "add") != 0 && strcmp(action, "delete") != 0)) {
        http_respond_json_error(res, 400,
            "Champs requis: 'domain', 'rule', et 'action' ('add' ou 'delete').");
        return;
    }

    char err[256];
    if (!tomoyo_validate_rule(rule, err, sizeof(err))) {
        http_respond_json_error(res, 422, err); /* syntax error -> UI shows inline */
        return;
    }

    int rc = (strcmp(action, "add") == 0)
                 ? tomoyo_apply_rule(domain, rule, user)
                 : tomoyo_delete_rule(domain, rule, user);

    if (rc != 0) {
        http_respond_json_error(res, 500, "Echec de l'application de la regle.");
        return;
    }

    http_respond_json(res, 200, "{\"status\":\"ok\"}");
}

static void api_rollback(const twm_config_t *cfg, http_request_t *req, http_response_t *res) {
    (void)cfg;
    char user[64];
    if (!require_auth(req, res, user, sizeof(user))) return;

    char err[256] = {0};
    if (tomoyo_rollback(err, sizeof(err)) != 0) {
        http_respond_json_error(res, 409, err[0] ? err : "Rollback impossible.");
        return;
    }
    http_respond_json(res, 200, "{\"status\":\"ok\"}");
}

/* ---------- API: logs / stats ---------- */

static void api_logs(const twm_config_t *cfg, http_request_t *req, http_response_t *res) {
    (void)cfg;
    char user[64];
    if (!require_auth(req, res, user, sizeof(user))) return;

    char vflag[8] = {0}, lflag[8] = {0};
    query_get(req->query, "violations", vflag, sizeof(vflag));
    query_get(req->query, "limit", lflag, sizeof(lflag));
    int violations_only = (vflag[0] == '1');
    int limit = lflag[0] ? atoi(lflag) : 200;
    if (limit <= 0 || limit > 2000) limit = 200;

    char *raw = tomoyo_read_logs(violations_only, limit);

    json_buf_t jb; jb_init(&jb);
    jb_char(&jb, '{');
    jb_key(&jb, "logs");
    jb_char(&jb, '[');
    char *line = raw;
    int first = 1;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (*line) {
            if (!first) jb_char(&jb, ',');
            jb_string(&jb, line);
            first = 0;
        }
        line = nl ? nl + 1 : NULL;
    }
    jb_char(&jb, ']');
    jb_char(&jb, '}');
    http_respond_json(res, 200, jb.buf);
    jb_free(&jb);
    free(raw);
}

static void api_stat(const twm_config_t *cfg, http_request_t *req, http_response_t *res) {
    (void)cfg;
    char user[64];
    if (!require_auth(req, res, user, sizeof(user))) return;

    char *raw = tomoyo_read_stat();

    json_buf_t jb; jb_init(&jb);
    jb_char(&jb, '{');
    char *line = raw;
    int first = 1;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char *k = trim(line);
            char *v = trim(colon + 1);
            if (*k) {
                if (!first) jb_char(&jb, ',');
                jb_kv_string(&jb, k, v);
                first = 0;
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    jb_char(&jb, '}');
    http_respond_json(res, 200, jb.buf);
    jb_free(&jb);
    free(raw);
}

static void api_history(const twm_config_t *cfg, http_request_t *req, http_response_t *res) {
    char user[64];
    if (!require_auth(req, res, user, sizeof(user))) return;

    char path[300];
    snprintf(path, sizeof(path), "%s/mode_history.log", cfg->data_dir);
    size_t len;
    char *data = read_file_all(path, &len);

    json_buf_t jb; jb_init(&jb);
    jb_char(&jb, '{');
    jb_key(&jb, "entries");
    jb_char(&jb, '[');
    if (data) {
        char *line = data;
        int first = 1;
        while (line && *line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (*line) {
                char ts[32] = {0}, action[32] = {0}, dom[TWM_DOMAIN_NAME_LEN] = {0},
                     by[64] = {0}, detail[TWM_RULE_LEN] = {0};
                sscanf(line, "%31[^\t]\t%31[^\t]\t%511[^\t]\t%63[^\t]\t%511[^\n]",
                       ts, action, dom, by, detail);
                if (!first) jb_char(&jb, ',');
                jb_char(&jb, '{');
                jb_kv_string(&jb, "ts", ts);
                jb_char(&jb, ',');
                jb_kv_string(&jb, "action", action);
                jb_char(&jb, ',');
                jb_kv_string(&jb, "domain", dom);
                jb_char(&jb, ',');
                jb_kv_string(&jb, "by", by);
                jb_char(&jb, ',');
                jb_kv_string(&jb, "detail", detail);
                jb_char(&jb, '}');
                first = 0;
            }
            line = nl ? nl + 1 : NULL;
        }
        free(data);
    }
    jb_char(&jb, ']');
    jb_char(&jb, '}');
    http_respond_json(res, 200, jb.buf);
    jb_free(&jb);
}

/* ---------- API: auto-generation (learning capture) ---------- */

static void api_generate(const twm_config_t *cfg, http_request_t *req, http_response_t *res) {
    char user[64];
    if (!require_auth(req, res, user, sizeof(user))) return;

    char exec_path[256] = {0};
    json_get_string(req->body, "exec_path", exec_path, sizeof(exec_path));
    if (exec_path[0] == '\0') {
        http_respond_json_error(res, 400, "Champ 'exec_path' manquant.");
        return;
    }

    char domain[TWM_DOMAIN_NAME_LEN] = {0};
    int pid = 0;
    char err[256] = {0};
    if (tomoyo_start_learning_capture(cfg, exec_path, domain, sizeof(domain),
                                       &pid, err, sizeof(err)) != 0) {
        http_respond_json_error(res, 403, err);
        return;
    }

    json_buf_t jb; jb_init(&jb);
    jb_char(&jb, '{');
    jb_kv_string(&jb, "status", "ok");
    jb_char(&jb, ',');
    jb_kv_string(&jb, "domain", domain);
    jb_char(&jb, ',');
    jb_kv_int(&jb, "pid", pid);
    jb_char(&jb, '}');
    http_respond_json(res, 200, jb.buf);
    jb_free(&jb);
}

static void api_export(const twm_config_t *cfg, http_request_t *req, http_response_t *res) {
    (void)cfg;
    char user[64];
    if (!require_auth(req, res, user, sizeof(user))) return;

    char domain[TWM_DOMAIN_NAME_LEN];
    query_get(req->query, "domain", domain, sizeof(domain));
    if (domain[0] == '\0') {
        http_respond_json_error(res, 400, "Parametre 'domain' manquant.");
        return;
    }

    char *policy = tomoyo_export_policy(domain);
    http_respond_file(res, 200, "text/plain; charset=utf-8", policy, strlen(policy));
    free(policy);
}

/* ---------- dispatcher ---------- */

void routes_dispatch(const twm_config_t *cfg, http_request_t *req, http_response_t *res) {
    const char *m = req->method;
    const char *p = req->path;

    if (strncmp(p, "/api/", 5) != 0) {
        if (strcmp(m, "GET") == 0) serve_static(cfg, p, res);
        else http_respond_text(res, 405, "text/plain", "Method Not Allowed");
        return;
    }

    if (strcmp(p, "/api/login") == 0 && strcmp(m, "POST") == 0) { api_login(cfg, req, res); return; }
    if (strcmp(p, "/api/logout") == 0 && strcmp(m, "POST") == 0) { api_logout(cfg, req, res); return; }
    if (strcmp(p, "/api/session") == 0 && strcmp(m, "GET") == 0) { api_session(cfg, req, res); return; }

    if (strcmp(p, "/api/domains") == 0 && strcmp(m, "GET") == 0) { api_domains(cfg, req, res); return; }
    if (strcmp(p, "/api/rules") == 0 && strcmp(m, "GET") == 0)   { api_rules(cfg, req, res); return; }
    if (strcmp(p, "/api/rules") == 0 && strcmp(m, "POST") == 0)  { api_rules_edit(cfg, req, res); return; }

    if (strcmp(p, "/api/mode") == 0 && strcmp(m, "POST") == 0)     { api_mode(cfg, req, res); return; }
    if (strcmp(p, "/api/rollback") == 0 && strcmp(m, "POST") == 0) { api_rollback(cfg, req, res); return; }

    if (strcmp(p, "/api/logs") == 0 && strcmp(m, "GET") == 0)    { api_logs(cfg, req, res); return; }
    if (strcmp(p, "/api/stat") == 0 && strcmp(m, "GET") == 0)    { api_stat(cfg, req, res); return; }
    if (strcmp(p, "/api/history") == 0 && strcmp(m, "GET") == 0) { api_history(cfg, req, res); return; }

    if (strcmp(p, "/api/generate") == 0 && strcmp(m, "POST") == 0) { api_generate(cfg, req, res); return; }
    if (strcmp(p, "/api/export") == 0 && strcmp(m, "GET") == 0)    { api_export(cfg, req, res); return; }

    http_respond_json_error(res, 404, "Route API inconnue.");
}
