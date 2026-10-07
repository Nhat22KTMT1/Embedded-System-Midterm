#include "ssd1306.h"
#include "driver/i2c_master.h"
#include "font5x7.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SSD1306";

#define SSD1306_PAGES (SSD1306_HEIGHT / 8)

static uint8_t s_buffer[SSD1306_WIDTH * SSD1306_PAGES];
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

static esp_err_t ssd1306_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd}; // 0x00: control byte bao day la lenh (command)
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

esp_err_t ssd1306_init(int sda_gpio, int scl_gpio, uint8_t i2c_addr)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong the tao I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong the them thiet bi I2C: %s", esp_err_to_name(err));
        return err;
    }

    /* Chuoi lenh khoi tao chuan cho SSD1306 128x64 */
    static const uint8_t init_cmds[] = {
        0xAE,       // display off
        0xD5, 0x80, // set display clock divide ratio/oscillator
        0xA8, 0x3F, // set multiplex ratio (64-1)
        0xD3, 0x00, // set display offset = 0
        0x40,       // set start line = 0
        0x8D, 0x14, // enable charge pump
        0x20, 0x00, // memory addressing mode: horizontal
        0xA1,       // segment remap
        0xC8,       // COM output scan direction
        0xDA, 0x12, // COM pins hardware config
        0x81, 0xCF, // contrast
        0xD9, 0xF1, // pre-charge period
        0xDB, 0x40, // VCOMH deselect level
        0xA4,       // resume to RAM content (khong phai display all-on)
        0xA6,       // normal display (khong dao mau)
        0xAF,       // display on
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        err = ssd1306_cmd(init_cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Loi gui lenh khoi tao #%d: %s", (int)i, esp_err_to_name(err));
            return err;
        }
    }

    ssd1306_clear();
    ssd1306_display();
    ESP_LOGI(TAG, "OLED SSD1306 da khoi tao thanh cong");
    return ESP_OK;
}

void ssd1306_clear(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
}

void ssd1306_draw_pixel(int x, int y, int color)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) {
        return;
    }
    int page = y / 8;
    int bit = y % 8;
    int idx = page * SSD1306_WIDTH + x;

    if (color) {
        s_buffer[idx] |= (uint8_t)(1 << bit);
    } else {
        s_buffer[idx] &= (uint8_t)~(1 << bit);
    }
}

void ssd1306_draw_hline(int x, int y, int w, int color)
{
    for (int i = 0; i < w; i++) {
        ssd1306_draw_pixel(x + i, y, color);
    }
}

void ssd1306_draw_vline(int x, int y, int h, int color)
{
    for (int i = 0; i < h; i++) {
        ssd1306_draw_pixel(x, y + i, color);
    }
}

void ssd1306_draw_rect(int x, int y, int w, int h, int color, int fill)
{
    if (fill) {
        for (int i = 0; i < h; i++) {
            ssd1306_draw_hline(x, y + i, w, color);
        }
    } else {
        ssd1306_draw_hline(x, y, w, color);
        ssd1306_draw_hline(x, y + h - 1, w, color);
        ssd1306_draw_vline(x, y, h, color);
        ssd1306_draw_vline(x + w - 1, y, h, color);
    }
}

void ssd1306_draw_char(int x, int y, char c, int color)
{
    const uint8_t *glyph = font5x7_get(c);
    for (int col = 0; col < 5; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                ssd1306_draw_pixel(x + col, y + row, color);
            }
        }
    }
}

void ssd1306_draw_string(int x, int y, const char *str, int color)
{
    int cursor_x = x;
    while (*str) {
        ssd1306_draw_char(cursor_x, y, *str, color);
        cursor_x += 6;
        str++;
    }
}

void ssd1306_display(void)
{
    static uint32_t i2c_error_count = 0;

    for (int page = 0; page < SSD1306_PAGES; page++) {
        uint8_t cmd_buf[4] = {
            0x00,
            (uint8_t)(0xB0 + page),
            0x00,
            0x10,
        };

        esp_err_t err = i2c_master_transmit(s_dev, cmd_buf, sizeof(cmd_buf), 100);
        if (err != ESP_OK) {
            i2c_error_count++;
            ESP_LOGE(TAG, "I2C OLED command loi page=%d: %s (count=%lu)",
                     page, esp_err_to_name(err), (unsigned long)i2c_error_count);
            return;
        }

        uint8_t data_buf[SSD1306_WIDTH + 1];
        data_buf[0] = 0x40;
        memcpy(&data_buf[1], &s_buffer[page * SSD1306_WIDTH], SSD1306_WIDTH);

        err = i2c_master_transmit(s_dev, data_buf, sizeof(data_buf), 100);
        if (err != ESP_OK) {
            i2c_error_count++;
            ESP_LOGE(TAG, "I2C OLED data loi page=%d: %s (count=%lu)",
                     page, esp_err_to_name(err), (unsigned long)i2c_error_count);
            return;
        }
    }
}
