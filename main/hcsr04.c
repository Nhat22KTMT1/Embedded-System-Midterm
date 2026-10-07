#include "hcsr04.h"

#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define HCSR04_TIMEOUT_MS        40
#define HCSR04_MIN_DISTANCE_CM   2.0f
#define HCSR04_MAX_DISTANCE_CM   400.0f

static gpio_num_t s_trig_gpio = GPIO_NUM_NC;
static gpio_num_t s_echo_gpio = GPIO_NUM_NC;

/*
 * Ban cu dung while(gpio_get_level(...)) de cho ECHO. Tren ESP32-C3 single-core,
 * vong busy-wait nay chay o task priority cao co the lam task_button/task_display
 * bi doi CPU. Ban nay dung GPIO interrupt + task notification: task do khoang cach
 * se BLOCK trong luc cho ECHO, CPU duoc nhuong cho cac task khac.
 */
static volatile bool s_measure_active = false;
static volatile int64_t s_rise_time_us = 0;
static volatile uint32_t s_echo_width_us = 0;
static TaskHandle_t s_waiting_task = NULL;

static void hcsr04_echo_isr(void *arg)
{
    if (!s_measure_active || s_waiting_task == NULL) {
        return;
    }

    int level = gpio_get_level(s_echo_gpio);
    int64_t now_us = esp_timer_get_time();

    if (level) {
        /* Canh len: bat dau xung ECHO. */
        s_rise_time_us = now_us;
    } else if (s_rise_time_us > 0) {
        /* Canh xuong: ket thuc xung ECHO. */
        int64_t width = now_us - s_rise_time_us;
        if (width > 0 && width <= 100000) {
            s_echo_width_us = (uint32_t)width;
        } else {
            s_echo_width_us = 0;
        }

        s_measure_active = false;

        BaseType_t higher_priority_task_woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_waiting_task, &higher_priority_task_woken);
        if (higher_priority_task_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

esp_err_t hcsr04_init(gpio_num_t trig_gpio, gpio_num_t echo_gpio)
{
    s_trig_gpio = trig_gpio;
    s_echo_gpio = echo_gpio;

    gpio_config_t trig_cfg = {
        .pin_bit_mask = 1ULL << trig_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&trig_cfg);
    if (err != ESP_OK) {
        return err;
    }

    gpio_config_t echo_cfg = {
        .pin_bit_mask = 1ULL << echo_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    err = gpio_config(&echo_cfg);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(s_trig_gpio, 0);

    /* Neu ISR service da duoc module khac cai roi thi ESP_ERR_INVALID_STATE la OK. */
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = gpio_isr_handler_add(s_echo_gpio, hcsr04_echo_isr, NULL);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

esp_err_t hcsr04_measure_cm(float *distance_cm)
{
    if (distance_cm == NULL ||
        s_trig_gpio == GPIO_NUM_NC ||
        s_echo_gpio == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Chi cho phep 1 phep do tai mot thoi diem. */
    if (s_measure_active) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Xoa notification cu neu co. */
    (void)ulTaskNotifyTake(pdTRUE, 0);

    s_waiting_task = xTaskGetCurrentTaskHandle();
    s_rise_time_us = 0;
    s_echo_width_us = 0;
    s_measure_active = true;

    /* Xung TRIG >= 10 us. */
    gpio_set_level(s_trig_gpio, 0);
    esp_rom_delay_us(2);
    gpio_set_level(s_trig_gpio, 1);
    esp_rom_delay_us(10);
    gpio_set_level(s_trig_gpio, 0);

    /* BLOCK thay vi busy-wait, de OLED va nut bam van chay muot. */
    uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HCSR04_TIMEOUT_MS));

    s_measure_active = false;
    s_waiting_task = NULL;

    if (notified == 0 || s_echo_width_us == 0) {
        return ESP_ERR_TIMEOUT;
    }

    const float measured_cm = ((float)s_echo_width_us * 0.0343f) / 2.0f;

    if (measured_cm < HCSR04_MIN_DISTANCE_CM ||
        measured_cm > HCSR04_MAX_DISTANCE_CM) {
        return ESP_FAIL;
    }

    *distance_cm = measured_cm;
    return ESP_OK;
}
