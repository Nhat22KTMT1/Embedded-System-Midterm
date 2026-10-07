#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64

/** Khoi tao I2C bus + OLED SSD1306 (dia chi thuong la 0x3C). */
esp_err_t ssd1306_init(int sda_gpio, int scl_gpio, uint8_t i2c_addr);

/** Xoa framebuffer trong RAM (chua day len man hinh). */
void ssd1306_clear(void);

/** Ve 1 diem anh. color: 1 = sang, 0 = tat. */
void ssd1306_draw_pixel(int x, int y, int color);

void ssd1306_draw_hline(int x, int y, int w, int color);
void ssd1306_draw_vline(int x, int y, int h, int color);

/** Ve hinh chu nhat. fill = 1: to dac; fill = 0: chi ve vien. */
void ssd1306_draw_rect(int x, int y, int w, int h, int color, int fill);

/** Ve 1 ky tu tai (x,y) (goc tren-trai cua ky tu). */
void ssd1306_draw_char(int x, int y, char c, int color);

/** Ve chuoi ky tu, moi ky tu cach nhau 6px (5px glyph + 1px khoang trong). */
void ssd1306_draw_string(int x, int y, const char *str, int color);

/** Day toan bo framebuffer trong RAM ra man hinh vat ly qua I2C. */
void ssd1306_display(void);

#ifdef __cplusplus
}
#endif
