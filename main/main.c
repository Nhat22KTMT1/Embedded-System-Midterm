/* ============================================================================
 * MINI WEATHER STATION - ESP32-C3 (ESP-IDF)
 * ----------------------------------------------------------------------------
 * Kien truc:
 *   task_sensor  (priority 5) -> sensor_queue  -> task_process (priority 3)
 *   task_water   (priority 3) -> water_queue   --------------------v
 *   task_button  (priority 5) -> button_queue   -----v
 *                                            display_queue -> [Queue Set] -> task_display (priority 4)
 *                                            button_queue  ------^
 *                                            water_queue   ------^
 *
 * - task_sensor  : doc DHT22 moi 2s, day du lieu tho vao sensor_queue
 * - task_process : nhan du lieu tho, tinh avg/min/max, luu NVS dinh ky,
 *                   gui du lieu da xu ly sang display_queue
 * - task_button  : doc nut nhan, gui lenh chuyen man hinh qua button_queue
 * - task_display : dung Queue Set de cho DONG THOI display_queue va
 *                   button_queue, ve len OLED SSD1306
 * ==========================================================================*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"

#include "dht22.h"
#include "ssd1306.h"
#include "hcsr04.h"
#include "rgb_led.h"
#include "buzzer.h"

static const char *TAG = "WEATHER_STATION";

/* ------------------------- Cau hinh chan GPIO ---------------------------- */
#define DHT22_GPIO      GPIO_NUM_4
#define OLED_SDA_GPIO   GPIO_NUM_5
#define OLED_SCL_GPIO   GPIO_NUM_6
#define BUTTON_GPIO     GPIO_NUM_7      // nut chuyen man hinh
/* Nut ACK/MUTE canh bao. GPIO9 thuong la nut BOOT tren ESP32-C3.
 * Co the dung nut BOOT san co khi he thong dang chay. KHONG giu nut nay
 * trong luc reset/flash vi GPIO9 la strapping pin vao Download Mode. */
#define ALARM_ACK_BUTTON_GPIO GPIO_NUM_9
#define HCSR04_TRIG_GPIO GPIO_NUM_1
#define HCSR04_ECHO_GPIO GPIO_NUM_10

/* LED RGB 4 chan. Mac dinh dung common cathode.
 * Moi kenh R/G/B PHAI co dien tro 220-330 ohm. */
#define RGB_RED_GPIO     GPIO_NUM_0
#define RGB_GREEN_GPIO   GPIO_NUM_3
#define RGB_BLUE_GPIO    GPIO_NUM_2
#define RGB_COMMON_ANODE false

/* Active buzzer canh bao khi muc nuoc dat nguong DANGER.
 * GPIO8 la strapping pin: khi flash firmware nen thao buzzer neu buzzer
 * lam anh huong boot mode, sau khi khoi dong co the gan lai. */
#define BUZZER_GPIO        GPIO_NUM_8
#define BUZZER_ACTIVE_HIGH true

#define OLED_I2C_ADDR   0x3C

#define HISTORY_LEN     10          // so mau luu de ve bieu do mini
#define SENSOR_PERIOD_MS 2000       // chu ky doc cam bien
#define NVS_SAVE_EVERY_N 10         // luu NVS moi N lan doc
#define WATER_PERIOD_MS  1000        // chu ky do muc nuoc

/* Hieu chuan be nuoc:
 * - EMPTY: khoang cach tu mat HC-SR04 den day be / mat nuoc khi be rong.
 * - FULL : khoang cach tu mat HC-SR04 den mat nuoc khi be day.
 * Sua 2 gia tri nay theo be thuc te cua ban. */
#define WATER_EMPTY_DISTANCE_CM 103.0f
#define WATER_FULL_DISTANCE_CM   3.0f

/* Nguong canh bao muc nuoc (%).
 * < 70%       : xanh la
 * 70 .. <90%  : vang
 * >= 90%      : do
 * HC-SR04 loi : xanh duong
 * Hysteresis 2% giup LED khong nhap nhay khi so do dao dong sat nguong. */
#define WATER_WARNING_PERCENT    70.0f
#define WATER_DANGER_PERCENT     90.0f
#define WATER_HYSTERESIS_PERCENT  2.0f

/* Cau hinh hien thi Screen 4 (dong ho do muc nuoc).
 * BIG_FONT: neu ssd1306_draw_string() cua ban khong ho tro tham so
 * "ti le chu" (tham so cuoi la mau), doi BIG_FONT thanh 1. */
#define SMALL_FONT  1
#define BIG_FONT    2

/* Ten nhom hien thi tren man hinh HOME.
 * Font hien tai chi ho tro ASCII, nen tranh dung ky tu tieng Viet co dau. */
#define GROUP_NAME      "NHOM IPIN"

/* ------------------------------ Kieu du lieu ------------------------------ */

typedef struct {
    float temperature;
    float humidity;
    uint32_t timestamp;
} raw_data_t;

typedef enum {
    SCREEN_HOME = 0,
    SCREEN_TEMP,
    SCREEN_HUMID,
    SCREEN_WATER,
    SCREEN_CHART,
    SCREEN_COUNT
} screen_mode_t;

typedef struct {
    float current_temp;
    float current_humid;
    float avg_temp, min_temp, max_temp;
    float avg_humid, min_humid, max_humid;
    float temp_history[HISTORY_LEN];
    float humid_history[HISTORY_LEN];
} display_data_t;

typedef struct {
    float distance_cm;      // khoang cach cam bien -> mat nuoc
    float level_cm;         // chieu cao cot nuoc tinh tu moc EMPTY
    float level_percent;    // 0..100%
    bool valid;
    bool danger_active;       // true khi muc nuoc dang o WATER_ALARM_DANGER
    bool alarm_acknowledged;  // true sau khi nguoi dung nhan ACK/MUTE
} water_data_t;

typedef enum {
    BUTTON_EVENT_NEXT_SCREEN = 0,
    BUTTON_EVENT_ACK_ALARM,
} button_event_t;

typedef enum {
    WATER_ALARM_UNKNOWN = 0,
    WATER_ALARM_NORMAL,
    WATER_ALARM_WARNING,
    WATER_ALARM_DANGER,
    WATER_ALARM_SENSOR_ERROR,
} water_alarm_state_t;

/* Cau truc luu vao NVS (thong ke tich luy) */
typedef struct {
    float sum_temp, min_temp, max_temp;
    float sum_humid, min_humid, max_humid;
    uint32_t sample_count;
} weather_stats_t;

/* ------------------------------ Queue & Queue Set ------------------------ */

static QueueHandle_t sensor_queue;   // raw_data_t   : task_sensor  -> task_process
static QueueHandle_t button_queue;   // button_event_t: task_button -> task_display (qua Queue Set)
static QueueHandle_t display_queue;  // display_data_t: task_process -> task_display (qua Queue Set)
static QueueHandle_t water_queue;    // water_data_t  : task_water   -> task_display (qua Queue Set)
static QueueSetHandle_t display_set; // gop display_queue + button_queue + water_queue

/* true neu OLED khoi tao thanh cong (co phan hoi tren I2C).
 * Neu false (chua lap OLED), task_display se in du lieu ra Serial Monitor
 * thay vi ve len man hinh, de van xem duoc ket qua khi chay thu. */
static bool s_oled_ready = false;

/* Latched ACK cua nguoi dung. Chi reset khi muc nuoc thoat khoi DANGER.
 * volatile vi duoc doc/ghi boi task_display va task_water. */
static volatile bool s_alarm_acknowledged = false;

/* ------------------------------ NVS helpers ------------------------------- */

static void nvs_save_stats(const weather_stats_t *stats)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("weather", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Khong mo duoc NVS: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(handle, "stats", stats, sizeof(weather_stats_t));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Loi luu NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Da luu thong ke vao NVS (so mau=%" PRIu32 ")", stats->sample_count);
    }

    nvs_close(handle);
}

static bool nvs_load_stats(weather_stats_t *stats)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("weather", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false; // chua co du lieu cu, la binh thuong o lan chay dau
    }

    size_t required_size = sizeof(weather_stats_t);
    err = nvs_get_blob(handle, "stats", stats, &required_size);
    nvs_close(handle);

    return (err == ESP_OK);
}

/* ------------------------------ Task: doc cam bien ------------------------ */

static void task_sensor(void *param)
{
    raw_data_t data;

    ESP_LOGI(TAG, "task_sensor bat dau, doc moi %d ms", SENSOR_PERIOD_MS);
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        dht22_reading_t reading;
        esp_err_t err = dht22_read(&reading);

        if (err == ESP_OK) {
            data.temperature = reading.temperature;
            data.humidity = reading.humidity;
            data.timestamp = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

            if (xQueueSend(sensor_queue, &data, pdMS_TO_TICKS(100)) != pdPASS) {
                ESP_LOGW(TAG, "sensor_queue day, bo qua 1 mau");
            }
        } else {
            ESP_LOGW(TAG, "Doc DHT22 that bai: %s", esp_err_to_name(err));
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}

/* ------------------------------ Task: xu ly du lieu ------------------------ */

static void task_process(void *param)
{
    raw_data_t raw;
    weather_stats_t stats = {0};
    float temp_history[HISTORY_LEN] = {0};
    float humid_history[HISTORY_LEN] = {0};
    int history_idx = 0;

    /* Khoi tao min/max o gia tri cuc bien de lan dau chac chan duoc cap nhat */
    stats.min_temp = 1000.0f;
    stats.max_temp = -1000.0f;
    stats.min_humid = 1000.0f;
    stats.max_humid = -1000.0f;

    /* Khoi phuc thong ke cu tu NVS neu co (minh hoa "luu lich su") */
    weather_stats_t saved;
    if (nvs_load_stats(&saved)) {
        stats = saved;
        ESP_LOGI(TAG, "Da khoi phuc thong ke cu tu NVS (so mau=%" PRIu32 ")", stats.sample_count);
    }

    ESP_LOGI(TAG, "task_process bat dau");

    while (1) {
        if (xQueueReceive(sensor_queue, &raw, portMAX_DELAY) == pdPASS) {

            /* --- Cap nhat thong ke --- */
            stats.sum_temp += raw.temperature;
            stats.sum_humid += raw.humidity;
            if (raw.temperature < stats.min_temp) stats.min_temp = raw.temperature;
            if (raw.temperature > stats.max_temp) stats.max_temp = raw.temperature;
            if (raw.humidity < stats.min_humid) stats.min_humid = raw.humidity;
            if (raw.humidity > stats.max_humid) stats.max_humid = raw.humidity;
            stats.sample_count++;

            /* --- Cap nhat lich su cho bieu do mini (nhiet do + do am) --- */
            temp_history[history_idx % HISTORY_LEN] = raw.temperature;
            humid_history[history_idx % HISTORY_LEN] = raw.humidity;
            history_idx++;

            /* --- Luu NVS dinh ky (khong luu moi lan de tranh mon flash) --- */
            if (stats.sample_count % NVS_SAVE_EVERY_N == 0) {
                nvs_save_stats(&stats);
            }

            /* --- Dong goi du lieu gui sang task_display --- */
            display_data_t disp = {
                .current_temp = raw.temperature,
                .current_humid = raw.humidity,
                .avg_temp = stats.sum_temp / stats.sample_count,
                .min_temp = stats.min_temp,
                .max_temp = stats.max_temp,
                .avg_humid = stats.sum_humid / stats.sample_count,
                .min_humid = stats.min_humid,
                .max_humid = stats.max_humid,
            };
            memcpy(disp.temp_history, temp_history, sizeof(temp_history));
            memcpy(disp.humid_history, humid_history, sizeof(humid_history));

            if (xQueueSend(display_queue, &disp, pdMS_TO_TICKS(100)) != pdPASS) {
                ESP_LOGW(TAG, "display_queue day, bo qua 1 lan cap nhat man hinh");
            }

            ESP_LOGI(TAG, "T=%.1fC H=%.1f%% | avgT=%.1f minT=%.1f maxT=%.1f",
                     raw.temperature, raw.humidity,
                     disp.avg_temp, disp.min_temp, disp.max_temp);
        }
    }
}

/* ------------------------------ Task: do muc nuoc -------------------------- */

static float clamp_float(float value, float min_v, float max_v)
{
    if (value < min_v) return min_v;
    if (value > max_v) return max_v;
    return value;
}

static const char *water_alarm_name(water_alarm_state_t state)
{
    switch (state) {
        case WATER_ALARM_NORMAL:       return "NORMAL/GREEN";
        case WATER_ALARM_WARNING:      return "WARNING/YELLOW";
        case WATER_ALARM_DANGER:       return "DANGER/RED";
        case WATER_ALARM_SENSOR_ERROR: return "SENSOR_ERROR/BLUE";
        default:                       return "UNKNOWN";
    }
}

static water_alarm_state_t water_alarm_update(water_alarm_state_t current,
                                               bool valid,
                                               float percent)
{
    if (!valid) {
        return WATER_ALARM_SENSOR_ERROR;
    }

    /* Khi vua khoi dong hoac vua thoat khoi loi cam bien, phan loai truc tiep. */
    if (current == WATER_ALARM_UNKNOWN || current == WATER_ALARM_SENSOR_ERROR) {
        if (percent >= WATER_DANGER_PERCENT)  return WATER_ALARM_DANGER;
        if (percent >= WATER_WARNING_PERCENT) return WATER_ALARM_WARNING;
        return WATER_ALARM_NORMAL;
    }

    switch (current) {
        case WATER_ALARM_NORMAL:
            if (percent >= WATER_DANGER_PERCENT)  return WATER_ALARM_DANGER;
            if (percent >= WATER_WARNING_PERCENT) return WATER_ALARM_WARNING;
            return WATER_ALARM_NORMAL;

        case WATER_ALARM_WARNING:
            if (percent >= WATER_DANGER_PERCENT) return WATER_ALARM_DANGER;
            if (percent < (WATER_WARNING_PERCENT - WATER_HYSTERESIS_PERCENT)) {
                return WATER_ALARM_NORMAL;
            }
            return WATER_ALARM_WARNING;

        case WATER_ALARM_DANGER:
            if (percent < (WATER_DANGER_PERCENT - WATER_HYSTERESIS_PERCENT)) {
                if (percent < (WATER_WARNING_PERCENT - WATER_HYSTERESIS_PERCENT)) {
                    return WATER_ALARM_NORMAL;
                }
                return WATER_ALARM_WARNING;
            }
            return WATER_ALARM_DANGER;

        default:
            return WATER_ALARM_UNKNOWN;
    }
}

static void water_alarm_apply_outputs(water_alarm_state_t state, bool acknowledged)
{
    /* LED RGB hien thi trang thai muc nuoc. */
    switch (state) {
        case WATER_ALARM_NORMAL:
            rgb_led_set_color(RGB_COLOR_GREEN);
            break;
        case WATER_ALARM_WARNING:
            rgb_led_set_color(RGB_COLOR_YELLOW);
            break;
        case WATER_ALARM_DANGER:
            rgb_led_set_color(RGB_COLOR_RED);
            break;
        case WATER_ALARM_SENSOR_ERROR:
            rgb_led_set_color(RGB_COLOR_BLUE);
            break;
        default:
            rgb_led_off();
            break;
    }

    /* Buzzer chi keu o muc DANGER/RED. Khi muc nuoc giam xuong duoi
     * WATER_DANGER_PERCENT - WATER_HYSTERESIS_PERCENT, state se thoat
     * DANGER va buzzer tat tu dong. */
    /* Neu nguoi dung da ACK trong cung mot dot DANGER, giu buzzer tat
     * cho toi khi muc nuoc thoat DANGER. LED van do de bao nguy hiem. */
    buzzer_set((state == WATER_ALARM_DANGER) && !acknowledged);
}

static void task_water(void *param)
{
    water_alarm_state_t alarm_state = WATER_ALARM_UNKNOWN;

    ESP_LOGI(TAG, "task_water bat dau, do moi %d ms", WATER_PERIOD_MS);

    /* Lech pha 500 ms so voi DHT22 de 2 giao thuc can timing us khong do nhau. */
    vTaskDelay(pdMS_TO_TICKS(500));
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        water_data_t water = {0};
        float distance_cm = 0.0f;
        esp_err_t err = hcsr04_measure_cm(&distance_cm);

        if (err == ESP_OK) {
            const float usable_height = WATER_EMPTY_DISTANCE_CM - WATER_FULL_DISTANCE_CM;
            float level_cm = WATER_EMPTY_DISTANCE_CM - distance_cm;
            float level_percent = 0.0f;

            level_cm = clamp_float(level_cm, 0.0f, usable_height);
            if (usable_height > 0.1f) {
                level_percent = (level_cm / usable_height) * 100.0f;
            }

            water.distance_cm = distance_cm;
            water.level_cm = level_cm;
            water.level_percent = clamp_float(level_percent, 0.0f, 100.0f);
            water.valid = true;

            ESP_LOGI(TAG, "WATER distance=%.1fcm level=%.1fcm (%.1f%%)",
                     water.distance_cm, water.level_cm, water.level_percent);
        } else {
            water.valid = false;
            ESP_LOGW(TAG, "HC-SR04 do that bai: %s", esp_err_to_name(err));
        }

        water_alarm_state_t new_alarm = water_alarm_update(alarm_state,
                                                             water.valid,
                                                             water.level_percent);

        /* ACK chi co hieu luc trong DOT danger hien tai. Khi da thoat DANGER,
         * tu dong xoa ACK de lan danger ke tiep lai bao coi + hien WARNING. */
        if (new_alarm != WATER_ALARM_DANGER && s_alarm_acknowledged) {
            s_alarm_acknowledged = false;
            ESP_LOGI(TAG, "Alarm ACK reset: water da thoat DANGER");
        }

        water_alarm_apply_outputs(new_alarm, s_alarm_acknowledged);
        water.danger_active = (new_alarm == WATER_ALARM_DANGER);
        water.alarm_acknowledged = s_alarm_acknowledged;

        if (new_alarm != alarm_state) {
            ESP_LOGI(TAG, "WATER ALARM -> %s", water_alarm_name(new_alarm));
            alarm_state = new_alarm;
        }

        if (xQueueSend(water_queue, &water, pdMS_TO_TICKS(50)) != pdPASS) {
            ESP_LOGW(TAG, "water_queue day, bo qua 1 mau");
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(WATER_PERIOD_MS));
    }
}

/* ------------------------------ Task: doc nut bam -------------------------- */

static void task_button(void *param)
{
    int last_screen_state = 1; // active-low
    int last_ack_state = 1;    // active-low

    ESP_LOGI(TAG, "task_button bat dau: SCREEN=GPIO%d ACK=GPIO%d",
             BUTTON_GPIO, ALARM_ACK_BUTTON_GPIO);

    while (1) {
        int screen_state = gpio_get_level(BUTTON_GPIO);
        int ack_state = gpio_get_level(ALARM_ACK_BUTTON_GPIO);

        /* Nut chuyen man hinh: gui event, task_display tu tang screen. */
        if (last_screen_state == 1 && screen_state == 0) {
            button_event_t event = BUTTON_EVENT_NEXT_SCREEN;
            if (xQueueSend(button_queue, &event, pdMS_TO_TICKS(50)) != pdPASS) {
                ESP_LOGW(TAG, "button_queue day (NEXT_SCREEN)");
            }
            ESP_LOGI(TAG, "Nut SCREEN duoc nhan");
            vTaskDelay(pdMS_TO_TICKS(120));
        }

        /* Nut ACK/MUTE: chi co tac dung dac biet khi DANGER dang active. */
        if (last_ack_state == 1 && ack_state == 0) {
            button_event_t event = BUTTON_EVENT_ACK_ALARM;
            if (xQueueSend(button_queue, &event, pdMS_TO_TICKS(50)) != pdPASS) {
                ESP_LOGW(TAG, "button_queue day (ACK_ALARM)");
            }
            ESP_LOGI(TAG, "Nut ACK/MUTE duoc nhan");
            vTaskDelay(pdMS_TO_TICKS(120));
        }

        last_screen_state = screen_state;
        last_ack_state = ack_state;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

/* ------------------------------ Task: hien thi OLED ------------------------ */

/* Man hinh dau tien: ten nhom + nhiet do + do am + muc nuoc hien tai. */
static void draw_home_screen(const display_data_t *d, const water_data_t *w)
{
    char line[24];

    ssd1306_draw_string(0, 0, "MINI WEATHER", 1);
    ssd1306_draw_string(0, 10, GROUP_NAME, 1);
    ssd1306_draw_hline(0, 19, SSD1306_WIDTH, 1);

    snprintf(line, sizeof(line), "TEMP : %.1f C", d->current_temp);
    ssd1306_draw_string(0, 25, line, 1);

    snprintf(line, sizeof(line), "HUMID: %.1f %%", d->current_humid);
    ssd1306_draw_string(0, 38, line, 1);

    if (w->valid) {
        snprintf(line, sizeof(line), "WATER: %.1f %%", w->level_percent);
    } else {
        snprintf(line, sizeof(line), "WATER: --");
    }
    ssd1306_draw_string(0, 51, line, 1);
}

/* Man hinh canh bao duoc ep hien thi trong suot thoi gian buzzer dang keu. */
static void draw_alert_screen(const water_data_t *w)
{
    char line[24];

    ssd1306_draw_string(35, 0, "WARNING", 1);
    ssd1306_draw_hline(0, 10, SSD1306_WIDTH, 1);
    ssd1306_draw_string(16, 17, "WATER LEVEL HIGH", 1);

    if (w->valid) {
        snprintf(line, sizeof(line), "LEVEL: %.1f %%", w->level_percent);
        ssd1306_draw_string(20, 31, line, 1);
    }

    ssd1306_draw_string(22, 44, "BUZZER ACTIVE", 1);
    ssd1306_draw_string(17, 56, "PRESS ACK BUTTON", 1);
}

static void draw_temp_screen(const display_data_t *d)
{
    char line[24];

    ssd1306_draw_string(0, 0, "SCREEN 2: TEMP", 1);
    ssd1306_draw_hline(0, 9, SSD1306_WIDTH, 1);

    snprintf(line, sizeof(line), "CUR : %.1fC", d->current_temp);
    ssd1306_draw_string(0, 16, line, 1);

    snprintf(line, sizeof(line), "AVG : %.1fC", d->avg_temp);
    ssd1306_draw_string(0, 28, line, 1);

    snprintf(line, sizeof(line), "MIN : %.1fC", d->min_temp);
    ssd1306_draw_string(0, 40, line, 1);

    snprintf(line, sizeof(line), "MAX : %.1fC", d->max_temp);
    ssd1306_draw_string(0, 52, line, 1);
}

static void draw_humid_screen(const display_data_t *d)
{
    char line[24];

    ssd1306_draw_string(0, 0, "SCREEN 3: HUMID", 1);
    ssd1306_draw_hline(0, 9, SSD1306_WIDTH, 1);

    snprintf(line, sizeof(line), "CUR : %.1f%%", d->current_humid);
    ssd1306_draw_string(0, 16, line, 1);

    snprintf(line, sizeof(line), "AVG : %.1f%%", d->avg_humid);
    ssd1306_draw_string(0, 28, line, 1);

    snprintf(line, sizeof(line), "MIN : %.1f%%", d->min_humid);
    ssd1306_draw_string(0, 40, line, 1);

    snprintf(line, sizeof(line), "MAX : %.1f%%", d->max_humid);
    ssd1306_draw_string(0, 52, line, 1);
}

/* Ve mot duong thang tho (Bresenham don gian, chi can chay tren OLED nen
 * khong can toi uu) tu (x0,y0) den (x1,y1). */
static void draw_line(int x0, int y0, int x1, int y1, int color)
{
    int dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        ssd1306_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Ve 1 bieu do DUONG (line chart) mini kem nhan "TAG:min-max" phia tren.
 * Dung duong noi diem thay vi cot: bieu do duong the hien dao dong nho
 * (vd nhiet do trong nha chi doi 0.1-0.3 do) ro rang hon nhieu so voi cot,
 * vi mat nguoi de nhan ra do doc/huong cua duong hon la chenh lech chieu
 * cao vai pixel giua cac cot.
 * section_y: toa do Y bat dau cua ca khoi (nhan chu + khung bieu do).
 * tag: 1 ky tu de phan biet (vd 'T' cho nhiet do, 'H' cho do am). */
static void draw_mini_chart(int section_y, char tag, const float *history,
                             float min_v, float max_v)
{
    char label[24];
    snprintf(label, sizeof(label), "%c:%.1f-%.1f", tag, min_v, max_v);
    ssd1306_draw_string(0, section_y, label, 1);

    int chart_x = 2, chart_w = 122;
    int chart_y = section_y + 9;   // cach nhan chu 2px (nhan cao 7px)
    int chart_h = 16;              // tong chieu cao khung bieu do

    /* Khung tham chieu: duong tren = muc MAX, duong duoi = muc MIN.
     * Day chinh la "thang do" truc quan tren OLED, khong chi doc bang so. */
    ssd1306_draw_hline(chart_x, chart_y, chart_w, 1);
    ssd1306_draw_hline(chart_x, chart_y + chart_h - 1, chart_w, 1);

    float range = (max_v - min_v) > 0.1f ? (max_v - min_v) : 1.0f;
    int step_x = chart_w / (HISTORY_LEN - 1);
    int usable_h = chart_h - 3; // chua 2 duong khung tren/duoi + 1px dem

    int prev_x = -1, prev_y = -1;
    for (int i = 0; i < HISTORY_LEN; i++) {
        float v = history[i];
        int offset = (int)(((v - min_v) / range) * usable_h);
        if (offset < 0) offset = 0;
        if (offset > usable_h) offset = usable_h;

        int x = chart_x + i * step_x;
        int y = (chart_y + chart_h - 2) - offset; // gia tri lon -> gan duong MAX (y nho)

        if (prev_x >= 0) {
            draw_line(prev_x, prev_y, x, y, 1);
        }
        ssd1306_draw_rect(x - 1, y - 1, 3, 3, 1, 1); // cham danh dau tung diem do

        prev_x = x;
        prev_y = y;
    }
}

/* ---- Screen 4: dong ho do muc nuoc (kieu "Muc nuoc: x/y cm" + thanh doc) ---- */

/* Ve thanh doc ben phai: khung + phan fill tu duoi len theo percent (0..100)
 * + vach nguong canh bao (threshold_percent) nhô ra ben trai thanh. */
static void draw_vertical_gauge(bool valid, float percent, float threshold_percent)
{
    const int bar_x = 112, bar_y = 2, bar_w = 12, bar_h = 60;
    const int in_x = bar_x + 2, in_w = bar_w - 4;
    const int in_y = bar_y + 2, in_h = bar_h - 4;

    /* Khung ngoai (4 duong thang) */
    draw_line(bar_x, bar_y, bar_x + bar_w - 1, bar_y, 1);                         // tren
    draw_line(bar_x, bar_y + bar_h - 1, bar_x + bar_w - 1, bar_y + bar_h - 1, 1); // duoi
    draw_line(bar_x, bar_y, bar_x, bar_y + bar_h - 1, 1);                         // trai
    draw_line(bar_x + bar_w - 1, bar_y, bar_x + bar_w - 1, bar_y + bar_h - 1, 1); // phai

    /* Phan fill (tu duoi len) */
    if (valid) {
        int fill_h = (int)((clamp_float(percent, 0.0f, 100.0f) / 100.0f) * in_h);
        for (int i = 0; i < fill_h; i++) {
            ssd1306_draw_hline(in_x, in_y + in_h - 1 - i, in_w, 1);
        }
    }

    /* Vach nguong canh bao: gach ngang nho nhin ra ben trai thanh */
    int ty = in_y + in_h - 1 - (int)((clamp_float(threshold_percent, 0.0f, 100.0f) / 100.0f) * in_h);
    ssd1306_draw_hline(bar_x - 4, ty, 4, 1);
}

static void draw_water_screen(const water_data_t *w)
{
    char lv[8], mx[8], buf[16];
    const float max_cm = WATER_EMPTY_DISTANCE_CM - WATER_FULL_DISTANCE_CM;
    const int cw = 6 * BIG_FONT;          // chieu rong 1 ky tu chu lon
    const int ch = 7 * BIG_FONT;          // chieu cao 1 ky tu chu lon
    int x = 0;

    /* --- Muc nuoc hien tai / toi da --- */
    ssd1306_draw_string(0, 0, "MUC NUOC:", SMALL_FONT);

    if (w->valid) {
        snprintf(lv, sizeof(lv), "%.0f", w->level_cm);
    } else {
        snprintf(lv, sizeof(lv), "--");
    }
    snprintf(mx, sizeof(mx), "%.0f", max_cm);

    ssd1306_draw_string(x, 10, lv, BIG_FONT);
    x += (int)strlen(lv) * cw;

    /* Dau "/" ve bang duong thang (font khong co glyph '/'), day 2px */
    draw_line(x + cw - 3, 10, x + 2, 10 + ch - 1, 1);
    draw_line(x + cw - 2, 10, x + 3, 10 + ch - 1, 1);
    x += cw;

    ssd1306_draw_string(x, 10, mx, BIG_FONT);
    x += (int)strlen(mx) * cw;
    ssd1306_draw_string(x + 3, 10, "CM", SMALL_FONT);

    /* --- Nguong canh bao theo % (muc DANGER) --- */
    ssd1306_draw_string(0, 34, "NGUONG CANH BAO:", SMALL_FONT);

    snprintf(buf, sizeof(buf), "%.0f%%", WATER_DANGER_PERCENT);
    ssd1306_draw_string(0, 44, buf, BIG_FONT);

    /* --- Thanh doc ben phai --- */
    draw_vertical_gauge(w->valid, w->level_percent, WATER_DANGER_PERCENT);
}

static void draw_chart_screen(const display_data_t *d)
{
    ssd1306_draw_string(0, 0, "SCREEN 5: CHART", 1);
    ssd1306_draw_hline(0, 9, SSD1306_WIDTH, 1);

    /* Nua tren: bieu do nhiet do (10 mau gan nhat), nhan "T:min-max" */
    draw_mini_chart(11, 'T', d->temp_history, d->min_temp, d->max_temp);

    /* Duong ke phan cach 2 bieu do */
    ssd1306_draw_hline(0, 37, SSD1306_WIDTH, 1);

    /* Nua duoi: bieu do do am (10 mau gan nhat), nhan "H:min-max" */
    draw_mini_chart(38, 'H', d->humid_history, d->min_humid, d->max_humid);
}

/* --- Phien ban "khong co OLED": in cung noi dung ra Serial Monitor --- */

static void log_home_screen(const display_data_t *d, const water_data_t *w)
{
    if (w->valid) {
        ESP_LOGI(TAG, "[HOME ] %s | T=%.1fC H=%.1f%% WATER=%.1f%%",
                 GROUP_NAME, d->current_temp, d->current_humid, w->level_percent);
    } else {
        ESP_LOGI(TAG, "[HOME ] %s | T=%.1fC H=%.1f%% WATER=NO SIGNAL",
                 GROUP_NAME, d->current_temp, d->current_humid);
    }
}

static void log_alert_screen(const water_data_t *w)
{
    if (w->valid) {
        ESP_LOGW(TAG, "[ALERT] WATER LEVEL HIGH: %.1f%% | BUZZER ACTIVE | PRESS ACK",
                 w->level_percent);
    } else {
        ESP_LOGW(TAG, "[ALERT] WATER LEVEL HIGH | BUZZER ACTIVE");
    }
}

static void log_temp_screen(const display_data_t *d)
{
    ESP_LOGI(TAG, "[TEMP ] Now=%.1fC  Avg=%.1fC  Min=%.1fC  Max=%.1fC",
             d->current_temp, d->avg_temp, d->min_temp, d->max_temp);
}

static void log_humid_screen(const display_data_t *d)
{
    ESP_LOGI(TAG, "[HUMID] Now=%.1f%%  Avg=%.1f%%  Min=%.1f%%  Max=%.1f%%",
             d->current_humid, d->avg_humid, d->min_humid, d->max_humid);
}

static void log_water_screen(const water_data_t *w)
{
    const float max_cm = WATER_EMPTY_DISTANCE_CM - WATER_FULL_DISTANCE_CM;

    if (w->valid) {
        ESP_LOGI(TAG, "[WATER] Muc nuoc=%.1f/%.0fcm (%.1f%%)  Nguong canh bao=%.0f%%  Distance=%.1fcm",
                 w->level_cm, max_cm, w->level_percent, WATER_DANGER_PERCENT, w->distance_cm);
    } else {
        ESP_LOGW(TAG, "[WATER] No signal");
    }
}

static void log_chart_screen(const display_data_t *d)
{
    char buf_t[HISTORY_LEN * 7 + 1];
    char buf_h[HISTORY_LEN * 7 + 1];
    int pos_t = 0, pos_h = 0;

    for (int i = 0; i < HISTORY_LEN && pos_t < (int)sizeof(buf_t) - 8; i++) {
        pos_t += snprintf(buf_t + pos_t, sizeof(buf_t) - pos_t, "%.1f ", d->temp_history[i]);
    }
    for (int i = 0; i < HISTORY_LEN && pos_h < (int)sizeof(buf_h) - 8; i++) {
        pos_h += snprintf(buf_h + pos_h, sizeof(buf_h) - pos_h, "%.1f ", d->humid_history[i]);
    }

    ESP_LOGI(TAG, "[CHART-T] (%.1f-%.1f) %s", d->min_temp, d->max_temp, buf_t);
    ESP_LOGI(TAG, "[CHART-H] (%.1f-%.1f) %s", d->min_humid, d->max_humid, buf_h);
}

static void task_display(void *param)
{
    QueueSetMemberHandle_t activated;
    display_data_t latest_data = {0};
    water_data_t latest_water = {0};

    /* requested_screen la man hinh nguoi dung chon bang nut.
     * Khi DANGER xay ra, ALERT ghi de tam thoi nhung requested_screen van duoc giu
     * de khoi phuc sau khi muc nuoc xuong duoi nguong nguy hiem. */
    screen_mode_t requested_screen = SCREEN_HOME;
    screen_mode_t current_screen = SCREEN_HOME;
    bool alert_active = false;

    ESP_LOGI(TAG, "task_display bat dau, Queue Set: display + button + water");
    if (!s_oled_ready) {
        ESP_LOGW(TAG, "OLED chua san sang -> se in du lieu ra Serial Monitor thay vi ve man hinh");
    }

    while (1) {
        activated = xQueueSelectFromSet(display_set, portMAX_DELAY);

        if (activated == display_queue) {
            xQueueReceive(display_queue, &latest_data, 0);

        } else if (activated == button_queue) {
            button_event_t event;
            if (xQueueReceive(button_queue, &event, 0) == pdPASS) {
                if (event == BUTTON_EVENT_NEXT_SCREEN) {
                    requested_screen = (screen_mode_t)((requested_screen + 1) % SCREEN_COUNT);
                    if (!alert_active) {
                        current_screen = requested_screen;
                    }
                    ESP_LOGI(TAG, "Chuyen man hinh yeu cau -> %d", (int)requested_screen);

                } else if (event == BUTTON_EVENT_ACK_ALARM) {
                    if (latest_water.danger_active && !s_alarm_acknowledged) {
                        /* Tat coi NGAY, khong can doi task_water den chu ky ke tiep. */
                        s_alarm_acknowledged = true;
                        latest_water.alarm_acknowledged = true;
                        buzzer_set(false);
                        alert_active = false;
                        current_screen = requested_screen;
                        ESP_LOGW(TAG, "DANGER ACK: buzzer OFF, dong ALERT; LED van RED");
                    } else {
                        ESP_LOGI(TAG, "ACK bo qua: hien tai khong co DANGER chua xac nhan");
                    }
                }
            }

        } else if (activated == water_queue) {
            if (xQueueReceive(water_queue, &latest_water, 0) == pdPASS) {
                if (latest_water.danger_active && !latest_water.alarm_acknowledged) {
                    if (!alert_active) {
                        alert_active = true;
                        ESP_LOGW(TAG, "OLED -> ALERT SCREEN (buzzer active)");
                    }
                } else if (alert_active) {
                    /* Het DANGER hoac da duoc ACK -> dong overlay canh bao. */
                    alert_active = false;
                    current_screen = requested_screen;
                    ESP_LOGI(TAG, "OLED -> restore screen %d", (int)current_screen);
                }
            }
        }

        if (s_oled_ready) {
            ssd1306_clear();

            if (alert_active) {
                draw_alert_screen(&latest_water);
            } else {
                switch (current_screen) {
                    case SCREEN_HOME:  draw_home_screen(&latest_data, &latest_water); break;
                    case SCREEN_TEMP:  draw_temp_screen(&latest_data);              break;
                    case SCREEN_HUMID: draw_humid_screen(&latest_data);             break;
                    case SCREEN_WATER: draw_water_screen(&latest_water);             break;
                    case SCREEN_CHART: draw_chart_screen(&latest_data);              break;
                    default: break;
                }
            }
            ssd1306_display();

        } else {
            if (alert_active) {
                log_alert_screen(&latest_water);
            } else {
                switch (current_screen) {
                    case SCREEN_HOME:  log_home_screen(&latest_data, &latest_water); break;
                    case SCREEN_TEMP:  log_temp_screen(&latest_data);               break;
                    case SCREEN_HUMID: log_humid_screen(&latest_data);              break;
                    case SCREEN_WATER: log_water_screen(&latest_water);              break;
                    case SCREEN_CHART: log_chart_screen(&latest_data);               break;
                    default: break;
                }
            }
        }
    }
}

/* ------------------------------ Idle Task Hook ----------------------------- */
/* Duoc goi lien tuc boi Idle Task moi khi khong con Task nao san sang chay.
 * Dung de uoc luong % thoi gian CPU ranh -> phuc vu danh gia hieu nang. */

void vApplicationIdleHook(void)
{
    static uint32_t idle_counter = 0;
    idle_counter++;
    // Khong duoc goi ham block/delay dai o day.
    // Co the doc bien idle_counter tu noi khac de tinh ty le CPU idle neu can.
}

/* --------------------------------- app_main -------------------------------- */

void app_main(void)
{
    ESP_LOGI(TAG, "=== Mini Weather Station - khoi dong ===");

    /* --- Khoi tao NVS --- */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* --- Khoi tao nut bam --- */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO) | (1ULL << ALARM_ACK_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));

    /* --- Khoi tao cam bien --- */
    ESP_ERROR_CHECK(dht22_init(DHT22_GPIO));
    ESP_ERROR_CHECK(hcsr04_init(HCSR04_TRIG_GPIO, HCSR04_ECHO_GPIO));

    /* --- Khoi tao LED RGB canh bao muc nuoc --- */
    ESP_ERROR_CHECK(rgb_led_init(RGB_RED_GPIO,
                                 RGB_GREEN_GPIO,
                                 RGB_BLUE_GPIO,
                                 RGB_COMMON_ANODE));

    /* --- Khoi tao active buzzer canh bao muc nuoc --- */
    ESP_ERROR_CHECK(buzzer_init(BUZZER_GPIO, BUZZER_ACTIVE_HIGH));

    /* --- Khoi tao OLED (khong ESP_ERROR_CHECK: neu chua lap OLED,
     * he thong van chay binh thuong, chi in du lieu ra Serial Monitor) --- */
    esp_err_t oled_err = ssd1306_init(OLED_SDA_GPIO, OLED_SCL_GPIO, OLED_I2C_ADDR);
    if (oled_err == ESP_OK) {
        s_oled_ready = true;
    } else {
        ESP_LOGW(TAG, "Khong tim thay OLED (%s) - bo qua, se in du lieu ra Serial Monitor",
                 esp_err_to_name(oled_err));
    }

    /* --- Tao cac Queue --- */
    sensor_queue  = xQueueCreate(10, sizeof(raw_data_t));
    button_queue  = xQueueCreate(5, sizeof(button_event_t));
    display_queue = xQueueCreate(5, sizeof(display_data_t));
    water_queue   = xQueueCreate(5, sizeof(water_data_t));

    if (!sensor_queue || !button_queue || !display_queue || !water_queue) {
        ESP_LOGE(TAG, "Khong the tao Queue, dung he thong");
        return;
    }

    /* --- Queue Set: display_queue + button_queue + water_queue --- */
    display_set = xQueueCreateSet(5 + 5 + 5);
    if (!display_set) {
        ESP_LOGE(TAG, "Khong the tao Queue Set");
        return;
    }
    ESP_ERROR_CHECK(xQueueAddToSet(display_queue, display_set) != pdPASS ? ESP_FAIL : ESP_OK);
    ESP_ERROR_CHECK(xQueueAddToSet(button_queue, display_set) != pdPASS ? ESP_FAIL : ESP_OK);
    ESP_ERROR_CHECK(xQueueAddToSet(water_queue, display_set) != pdPASS ? ESP_FAIL : ESP_OK);

    /* --- Tao cac Task ---
     * ESP32-C3 la single-core. DHT22 can polling us nen giu priority 5,
     * nhung HC-SR04 ban moi dung interrupt + notification nen khong can priority cao.
     * Nut va display duoc uu tien de tranh cam giac UI bi freeze.
     *
     * task_sensor  : 5
     * task_button  : 5
     * task_display : 4
     * task_process : 3
     * task_water   : 3
     */
    xTaskCreate(task_sensor,  "task_sensor",  4096, NULL, 5, NULL);
    xTaskCreate(task_button,  "task_button",  2048, NULL, 5, NULL);
    xTaskCreate(task_display, "task_display", 4096, NULL, 4, NULL);
    xTaskCreate(task_process, "task_process", 4096, NULL, 3, NULL);
    xTaskCreate(task_water,   "task_water",   3072, NULL, 3, NULL);

    ESP_LOGI(TAG, "Da tao xong 5 Task + 4 Queue + 1 Queue Set + RGB + buzzer water alarm + ACK button. He thong dang chay.");
}