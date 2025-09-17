/* Simple Access Point with HTTP Server Example

   Wi-Fi SoftAP Example + Simple HTTPD Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "include/httpserver.h"
#include "busrtx.h"
#include "config.h"
#include "esp_err.h"
#include "include/otaupload.h"
#include "../../main/include/setupcommand.h"
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <stdint.h>
#include <strings.h>
#include <sys/param.h>
#include "esp_wifi_types_generic.h"
#include "lwip/err.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include <esp_http_server.h>
#include <string.h>
#include "freertos/task.h"
#include "include/utils.h"
#include "include/panelbuttons.h"
#include <esp_http_server.h>

/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */

static const char *TAG = "httpserver";

httpd_handle_t server = NULL;

/******************************************
*	WEBSOCKET
*******************************************/
typedef struct{
    httpd_handle_t hd;
    int fd;
}current_opened_ws_t;

current_opened_ws_t currentConnection;

bool isWsConnected(){
	return currentConnection.hd ? true : false;
}

void sendStringToWs(char *msg){
	if(currentConnection.hd){
		httpd_ws_frame_t wsFrame;
	    memset(&wsFrame, 0, sizeof(httpd_ws_frame_t));
	    wsFrame.payload = (uint8_t*)msg;
	    wsFrame.len = strlen(msg);
	    wsFrame.final = true;
	    wsFrame.fragmented = false;
	    wsFrame.type = HTTPD_WS_TYPE_TEXT;
		esp_err_t ret = httpd_ws_send_frame_async(currentConnection.hd, currentConnection.fd, &wsFrame);
		if (ret != ESP_OK) {
        	ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
    	}
	}
} 

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
		currentConnection.hd = req->handle;
		currentConnection.fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
    if (ws_pkt.len) {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
        
        if(ws_pkt.payload[0]=='>' && ((ws_pkt.len-1)%2)==0){
			// messaggio da inviare nel bus
			uint dataLen = (ws_pkt.len-1) / 2;
			uint8_t data[dataLen];
			err_t err = putCharHexToByteArray(data, dataLen, (char *)ws_pkt.payload+1);
			if(err==ESP_OK){
				hibusTxData(data, dataLen, TX_MODE_SINGLE_LOW_PRIORITY);
			}else{
				ESP_LOGE(TAG, "Error Coversion %s", ws_pkt.payload+1);	
			}
		}
        
    }
    ESP_LOGI(TAG, "Packet type: %d", ws_pkt.type);
//    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT &&
//        strcmp((char*)ws_pkt.payload,"Trigger async") == 0) {
//        free(buf);
//        return trigger_async_send(req->handle, req);
//    }

    ret = httpd_ws_send_frame(req, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
    }
    free(buf);
    return ret;
}

static const httpd_uri_t ws = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = ws_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = false
};

/******************************************/

/*
 * Serve OTA update portal (index.html)
 */
extern const uint8_t config_html_start[] asm("_binary_config_html_start");
extern const uint8_t config_html_end[] asm("_binary_config_html_end");

static esp_err_t send_default_page(httpd_req_t *req){
	esp_err_t ret;
	ret = httpd_resp_send(req, (const char *) config_html_start, config_html_end - config_html_start);
    return ret;
}

static const httpd_uri_t defaultPage = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = send_default_page,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
};

static const httpd_uri_t chInfoHandler = {
    .uri       = "/chinfo",
    .method    = HTTP_GET,
    .handler   = sendChannelInfo,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
};

/* An HTTP POST handler */
static esp_err_t echo_post_handler(httpd_req_t *req)
{
	char buf[150];
	memset(buf, 0x0, 150);
	if(req->content_len > 150){
		httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "set len > 150");
		return ESP_FAIL;
	}
    
	int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <=0) {
        /* Retry receiving if timeout occurred */
        return ESP_FAIL;
    }

    /* Log data received */
    ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
    ESP_LOGI(TAG, "%.*s", ret, buf);
    ESP_LOGI(TAG, "====================================");
	
	char retMsg[50];
	// cancello il buffer
	memset(retMsg, 0x0, 50);
	
//    *buf +=4 ;
    processSetupCommand(buf,retMsg);
    
    ESP_LOGI(TAG, "RetMsg: '%s'", retMsg);
    
    // End response
//	httpd_resp_send_chunk(req, (const char *) config_html_start, config_html_end - config_html_start);
//	httpd_resp_send_chunk(req, "<br/>", HTTPD_RESP_USE_STRLEN);
	httpd_resp_send_chunk(req, "<a href=\"/\">Return</a><br></br>", HTTPD_RESP_USE_STRLEN);
	httpd_resp_send_chunk(req, retMsg, strlen(retMsg));
    return ESP_OK;
}

static const httpd_uri_t setPost = {
    .uri       = "/",
    .method    = HTTP_POST,
    .handler   = echo_post_handler,
    .user_ctx  = NULL
};

static httpd_handle_t start_webserver(void)
{
//    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {

		//****************************************
		// OTA!!
		//****************************************
		otaInit(server, config);
		//****************************************
		
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &defaultPage);
        httpd_register_uri_handler(server, &chInfoHandler);
        httpd_register_uri_handler(server, &setPost);
        httpd_register_uri_handler(server, &ws);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}
//------------------------------------------------------------------------------

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

esp_err_t wifi_init_softap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

	static const size_t ssidLen = 23;

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = {0},
            .ssid_len = ssidLen,
            .max_connection = 3,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    
	// leggo il mac del wifi
    unsigned char mac_base[6] = {0};
    esp_efuse_mac_get_default(mac_base);
    esp_read_mac(mac_base, ESP_MAC_WIFI_STA);
	
	
	strcat((char*)wifi_config.ap.ssid, "SebyD24R8O_");
	btox((char *)&wifi_config.ap.ssid + ssidLen - 12, mac_base, 12);
//	wifi_config.ap.ssid[20] =0;
    
//    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
//        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
//    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
//    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s",
             wifi_config.ap.ssid, wifi_config.ap.password);
    return ESP_OK;
}
//------------------------------------------------------------------------------
bool wifiEnabled=false;
void wifiToggle(){
	if(wifiEnabled){
		httpd_stop(&server);
		ESP_ERROR_CHECK(esp_wifi_stop());
		ledColors[NUM_OF_LEDS-1] = ledSwitchOffColor;
	    updateLedColors();
		wifiEnabled=false;
	}else{
		ESP_ERROR_CHECK(esp_wifi_start());
		start_webserver();
		ledColors[NUM_OF_LEDS-1] = ledHttpEnableColor;
	    updateLedColors();
		wifiEnabled=true;
	}
}

void initApWebServer(bool startWebServer)
{
//    static httpd_handle_t server = NULL;

    ESP_LOGI(TAG, "NVS init");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    
    ESP_LOGI(TAG, "Eventloop create");
    esp_event_loop_create_default();

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
   
    ESP_LOGI(TAG, "init softAP");
    ESP_ERROR_CHECK(wifi_init_softap());

    /* Start the server for the first time */
    if(startWebServer){
		ESP_ERROR_CHECK(esp_wifi_start());
    	start_webserver();
    }
}
