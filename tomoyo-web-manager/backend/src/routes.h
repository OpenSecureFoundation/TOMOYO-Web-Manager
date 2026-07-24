#ifndef TWM_ROUTES_H
#define TWM_ROUTES_H

#include "config.h"
#include "http_server.h"

void routes_dispatch(const twm_config_t *cfg, http_request_t *req, http_response_t *res);

#endif
