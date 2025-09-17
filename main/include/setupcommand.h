#pragma once

#include "esp_http_server.h"
#include <strings.h>
#include <regex.h>
#include <sys/cdefs.h>

void processSetupCommand(char *msg, char *retMsg);
esp_err_t sendChannelInfo(httpd_req_t *req);



