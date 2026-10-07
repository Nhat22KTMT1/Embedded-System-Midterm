#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature;   // do C
    float humidity;      // % RH
} dht22_reading_t;

/**
 * Khoi tao chan GPIO dung cho DHT22 (che do open-drain + pull-up noi).
 */
esp_err_t dht22_init(int gpio_num);

/**
 * Doc mot lan tu cam bien DHT22.
 * Luu y: DHT22 can toi thieu ~2s giua 2 lan doc lien tiep.
 * Tra ve ESP_OK neu thanh cong, ESP_ERR_TIMEOUT hoac ESP_ERR_INVALID_CRC neu loi.
 */
esp_err_t dht22_read(dht22_reading_t *out);

#ifdef __cplusplus
}
#endif
