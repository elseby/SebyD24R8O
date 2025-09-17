# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS 
    	"main.c" 
    	"setupcommand.c" 
    	"decoder.c" 
    	"channels.c" 
    	"httpserver.c" 
    	"panelbuttons.c"
    	"busrtx.c"
    	"otaupload.c"
    	"output.c"
    	"utils.c"
    	"buzzer.c"
    	
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS
    REQUIRES 
    	nvs_flash 
    	esp_http_server 
    	esp_wifi 
    	esp_eth 
    	esp_driver_rmt 
    	led_strip 
    	esp_driver_gpio 
    	bootloader_support 
    	esp_timer
    	app_update
    	esp_driver_i2c
    PRIV_REQUIRES
	EMBED_TXTFILES 
		web/config.html    
		web/otaupload.html
)
