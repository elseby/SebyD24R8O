#pragma once

#include <stdint.h>
#include <stdint.h>
#include <sys/cdefs.h>
#include "config.h"
#include "driver/i2c_types.h"
#include "soc/gpio_num.h"

typedef struct {
	uint32_t red;
	uint32_t green;
	uint32_t blue;
} LedColor;

extern LedColor ledColors[NUM_OF_LEDS];

static const LedColor ledHttpEnableColor = {.red=0,.green=4,.blue=0};
static const LedColor ledDefaultOffColor = {.red=1,.green=1,.blue=3};

static const LedColor ledSwitchOnColor = {.red=6,.green=3,.blue=0};
static const LedColor ledSwitchOffColor = ledDefaultOffColor;

static const LedColor ledCoverActiveColor = {.red=1,.green=5,.blue=1};;
static const LedColor ledCoverInactiveOppColor = {.red=6,.green=1,.blue=1};
static const LedColor ledCoverStopColor = ledDefaultOffColor;

static const LedColor ledThermoOnColor = {.red=4,.green=2,.blue=4};
static const LedColor ledThermoOffColor = ledDefaultOffColor;

static const LedColor ledUnsetColor = {.red=0,.green=0,.blue=1};

void panelButtonsInit(i2c_master_bus_handle_t i2c_handle);

void setAllLed(LedColor color);
void updateLedColors(void);

static const gpio_num_t led_strip_gpio = GPIO_LEDSTRIP;


