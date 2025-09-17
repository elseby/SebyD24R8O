/*
 * otaupload.h
 *
 *  Created on: 3 apr 2025
 *      Author: elseby
 */

#pragma once

#include <esp_http_server.h>

void otaInit(httpd_handle_t http_server, httpd_config_t config);

