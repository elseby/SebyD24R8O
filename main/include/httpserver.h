#pragma once

/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/

#include <stdbool.h>

void initApWebServer(bool startWebServer);
bool isWsConnected();
void sendStringToWs(char *msg);
void wifiToggle();
