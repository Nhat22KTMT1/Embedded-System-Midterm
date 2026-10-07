#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

typedef enum {
    RGB_COLOR_OFF = 0,
    RGB_COLOR_RED,
    RGB_COLOR_GREEN,
    RGB_COLOR_YELLOW,
    RGB_COLOR_BLUE,
} rgb_color_t;

esp_err_t rgb_led_init(gpio_num_t red_gpio,
                       gpio_num_t green_gpio,
                       gpio_num_t blue_gpio,
                       bool common_anode);

void rgb_led_set_color(rgb_color_t color);
void rgb_led_off(void);
