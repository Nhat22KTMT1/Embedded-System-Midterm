#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Tra ve con tro toi bitmap 5 byte (5 cot x 7 dong, bit0 = dong tren cung)
 * cua ky tu c.
 *
 * Ho tro:
 *   - Chu in hoa A-Z
 *   - Chu so 0-9
 *   - Khoang trang va cac ky tu: . : - %
 *
 * Ky tu khong ho tro se duoc hien thi thanh khoang trang.
 */
const uint8_t *font5x7_get(char c);

#ifdef __cplusplus
}
#endif
