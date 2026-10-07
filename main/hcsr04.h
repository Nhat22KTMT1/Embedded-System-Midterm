#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Khoi tao HC-SR04.
 * trig_gpio: chan OUTPUT tu ESP32-C3 -> TRIG
 * echo_gpio: chan INPUT tu ECHO -> ESP32-C3
 *
 * LUU Y: ECHO cua HC-SR04 co muc HIGH ~5V khi module cap 5V.
 * Phai ha ap xuong ~3.3V truoc khi dua vao ESP32-C3.
 */
esp_err_t hcsr04_init(gpio_num_t trig_gpio, gpio_num_t echo_gpio);

/**
 * Do khoang cach tu mat cam bien den vat phan xa.
 * Tra ve ESP_OK neu thanh cong, ESP_ERR_TIMEOUT neu khong nhan duoc echo.
 */
esp_err_t hcsr04_measure_cm(float *distance_cm);

#ifdef __cplusplus
}
#endif
