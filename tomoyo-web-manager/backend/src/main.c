#include "config.h"
#include "auth.h"
#include "tomoyo_iface.h"
#include "http_server.h"
#include "routes.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static void *reaper_loop(void *arg) {
    (void)arg;
    for (;;) {
        sleep(60);
        auth_reap_expired();
    }
    return NULL;
}

int main(int argc, char **argv) {
    const char *config_path = "./config/tomoyo-web-manager.conf";
    if (argc > 1) config_path = argv[1];

    twm_config_t cfg;
    config_defaults(&cfg);
    config_load(config_path, &cfg);

    util_log(LOG_INFO, "TOMOYO-Web-Manager starting (config: %s)", config_path);
    util_log(LOG_INFO, "bind=%s:%d static_root=%s data_dir=%s tomoyo_root=%s",
             cfg.bind_addr, cfg.port, cfg.static_root, cfg.data_dir, cfg.tomoyo_root);

    auth_init(&cfg);
    int real_mode = tomoyo_iface_init(&cfg);
    if (!real_mode) {
        util_log(LOG_WARN,
                 "==> MODE DEMO/MOCK ACTIF : aucune interaction avec un vrai noyau TOMOYO. "
                 "Consultez docs/DEPLOIEMENT.md pour activer le module noyau reel.");
    }

    pthread_t reaper;
    pthread_create(&reaper, NULL, reaper_loop, NULL);
    pthread_detach(reaper);

    return http_server_run(&cfg, routes_dispatch) == 0 ? 0 : 1;
}
