#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

/**
 * @brief Khoi tao active buzzer dieu khien bang GPIO.
 *
 * @param gpio        GPIO noi voi chan dieu khien buzzer.
 * @param active_high true: HIGH = keu, false: LOW = keu.
 */
esp_err_t buzzer_init(gpio_num_t gpio, bool active_high);

/** Bat buzzer. */
void buzzer_on(void);

/** Tat buzzer. */
void buzzer_off(void);

/** Dat trang thai buzzer. */
void buzzer_set(bool on);

#endif
