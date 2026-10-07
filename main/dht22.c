#include "dht22.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DHT22";
static int s_gpio = -1;

esp_err_t dht22_init(int gpio_num)
{
    s_gpio = gpio_num;

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD, // open-drain: cho phep ca 2 chieu tren 1 day
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(gpio_num, 1);
    return ESP_OK;
}

/* Cho toi khi chan dat muc `level`, timeout tinh bang micro giay.
 * Tra ve false neu timeout (loi giao tiep). */
static bool wait_level(int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(s_gpio) != level) {
        if (esp_timer_get_time() - start > timeout_us) {
            return false;
        }
    }
    return true;
}

esp_err_t dht22_read(dht22_reading_t *out)
{
    if (s_gpio < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[5] = {0, 0, 0, 0, 0};

    /* --- Tin hieu bat dau (start signal) tu MCU --- */
    gpio_set_level(s_gpio, 0);
    esp_rom_delay_us(1200);   // keo xuong thap toi thieu 1ms (dung 1.2ms cho an toan)
    gpio_set_level(s_gpio, 1);
    esp_rom_delay_us(30);     // tha len, cho 20-40us

    /* --- Cho phan hoi tu cam bien --- */
    if (!wait_level(0, 100)) {
        ESP_LOGW(TAG, "Timeout: cam bien khong phan hoi (buoc 1)");
        return ESP_ERR_TIMEOUT;
    }
    if (!wait_level(1, 100)) {
        ESP_LOGW(TAG, "Timeout: cam bien khong phan hoi (buoc 2)");
        return ESP_ERR_TIMEOUT;
    }
    if (!wait_level(0, 100)) {
        ESP_LOGW(TAG, "Timeout: khong thay bit dau tien");
        return ESP_ERR_TIMEOUT;
    }

    /* --- Doc 40 bit du lieu (5 byte: humid_h, humid_l, temp_h, temp_l, checksum) --- */
    for (int i = 0; i < 40; i++) {
        if (!wait_level(1, 100)) {
            ESP_LOGW(TAG, "Timeout tai bit %d (cho muc cao)", i);
            return ESP_ERR_TIMEOUT;
        }

        int64_t t_high_start = esp_timer_get_time();

        if (!wait_level(0, 100)) {
            ESP_LOGW(TAG, "Timeout tai bit %d (cho muc thap)", i);
            return ESP_ERR_TIMEOUT;
        }

        int64_t high_duration = esp_timer_get_time() - t_high_start;

        int byte_idx = i / 8;
        data[byte_idx] <<= 1;
        // Xung cao ~26-28us => bit 0; xung cao ~70us => bit 1. Nguong 40us la an toan.
        if (high_duration > 40) {
            data[byte_idx] |= 1;
        }
    }

    /* --- Kiem tra checksum --- */
    uint8_t checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4]) {
        ESP_LOGW(TAG, "Checksum khong khop: nhan %02X, tinh duoc %02X", data[4], checksum);
        return ESP_ERR_INVALID_CRC;
    }

    int16_t raw_humidity = (int16_t)((data[0] << 8) | data[1]);
    int16_t raw_temp     = (int16_t)((data[2] << 8) | data[3]);

    bool negative = raw_temp & 0x8000;
    raw_temp &= 0x7FFF;

    out->humidity = raw_humidity / 10.0f;
    out->temperature = raw_temp / 10.0f;
    if (negative) {
        out->temperature = -out->temperature;
    }

    return ESP_OK;
}
