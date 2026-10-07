#include "rgb_led.h"
#include "driver/gpio.h"

static gpio_num_t s_red_gpio = GPIO_NUM_NC;
static gpio_num_t s_green_gpio = GPIO_NUM_NC;
static gpio_num_t s_blue_gpio = GPIO_NUM_NC;
static bool s_common_anode = false;
static bool s_initialized = false;

static inline int led_level(bool on)
{
    /* Common cathode: HIGH = on. Common anode: LOW = on. */
    return s_common_anode ? !on : on;
}

static void set_channels(bool red, bool green, bool blue)
{
    if (!s_initialized) {
        return;
    }

    gpio_set_level(s_red_gpio, led_level(red));
    gpio_set_level(s_green_gpio, led_level(green));
    gpio_set_level(s_blue_gpio, led_level(blue));
}

esp_err_t rgb_led_init(gpio_num_t red_gpio,
                       gpio_num_t green_gpio,
                       gpio_num_t blue_gpio,
                       bool common_anode)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(red_gpio) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(green_gpio) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(blue_gpio)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_red_gpio = red_gpio;
    s_green_gpio = green_gpio;
    s_blue_gpio = blue_gpio;
    s_common_anode = common_anode;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << red_gpio) | (1ULL << green_gpio) | (1ULL << blue_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    s_initialized = true;
    rgb_led_off();
    return ESP_OK;
}

void rgb_led_set_color(rgb_color_t color)
{
    switch (color) {
        case RGB_COLOR_RED:
            set_channels(true, false, false);
            break;
        case RGB_COLOR_GREEN:
            set_channels(false, true, false);
            break;
        case RGB_COLOR_YELLOW:
            set_channels(true, true, false);
            break;
        case RGB_COLOR_BLUE:
            set_channels(false, false, true);
            break;
        case RGB_COLOR_OFF:
        default:
            set_channels(false, false, false);
            break;
    }
}

void rgb_led_off(void)
{
    rgb_led_set_color(RGB_COLOR_OFF);
}
