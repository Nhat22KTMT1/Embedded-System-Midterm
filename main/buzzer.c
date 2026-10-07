#include "buzzer.h"

static gpio_num_t s_buzzer_gpio = GPIO_NUM_NC;
static bool s_active_high = true;
static bool s_initialized = false;

static inline int buzzer_level(bool on)
{
    if (s_active_high) {
        return on ? 1 : 0;
    }
    return on ? 0 : 1;
}

esp_err_t buzzer_init(gpio_num_t gpio, bool active_high)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_buzzer_gpio = gpio;
    s_active_high = active_high;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << gpio),
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
    buzzer_off();
    return ESP_OK;
}

void buzzer_set(bool on)
{
    if (!s_initialized) {
        return;
    }
    gpio_set_level(s_buzzer_gpio, buzzer_level(on));
}

void buzzer_on(void)
{
    buzzer_set(true);
}

void buzzer_off(void)
{
    buzzer_set(false);
}
