#include "font5x7.h"

/*
 * Font 5x7 cho SSD1306.
 * Moi ky tu gom 5 byte = 5 cot, bit0 la pixel tren cung.
 * Ho tro day du chu in hoa A-Z, chu so 0-9 va mot so ky tu dung trong project.
 */

static const uint8_t GLYPH_SPACE[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t GLYPH_DOT[5]   = {0x00, 0x60, 0x60, 0x00, 0x00};
static const uint8_t GLYPH_COLON[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
static const uint8_t GLYPH_DASH[5]  = {0x08, 0x08, 0x08, 0x08, 0x08};
static const uint8_t GLYPH_PCT[5]   = {0x62, 0x64, 0x08, 0x13, 0x23};

static const uint8_t GLYPH_0[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
static const uint8_t GLYPH_1[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
static const uint8_t GLYPH_2[5] = {0x42, 0x61, 0x51, 0x49, 0x46};
static const uint8_t GLYPH_3[5] = {0x21, 0x41, 0x45, 0x4B, 0x31};
static const uint8_t GLYPH_4[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
static const uint8_t GLYPH_5[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
static const uint8_t GLYPH_6[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
static const uint8_t GLYPH_7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
static const uint8_t GLYPH_8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
static const uint8_t GLYPH_9[5] = {0x06, 0x49, 0x49, 0x29, 0x1E};

/* A-Z */
static const uint8_t GLYPH_A[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
static const uint8_t GLYPH_B[5] = {0x7F, 0x49, 0x49, 0x49, 0x36};
static const uint8_t GLYPH_C[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
static const uint8_t GLYPH_D[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
static const uint8_t GLYPH_E[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
static const uint8_t GLYPH_F[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
static const uint8_t GLYPH_G[5] = {0x3E, 0x41, 0x49, 0x49, 0x7A};
static const uint8_t GLYPH_H[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
static const uint8_t GLYPH_I[5] = {0x00, 0x41, 0x7F, 0x41, 0x00};
static const uint8_t GLYPH_J[5] = {0x20, 0x40, 0x41, 0x3F, 0x01};
static const uint8_t GLYPH_K[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};
static const uint8_t GLYPH_L[5] = {0x7F, 0x40, 0x40, 0x40, 0x40};
static const uint8_t GLYPH_M[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
static const uint8_t GLYPH_N[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
static const uint8_t GLYPH_O[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
static const uint8_t GLYPH_P[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
static const uint8_t GLYPH_Q[5] = {0x3E, 0x41, 0x51, 0x21, 0x5E};
static const uint8_t GLYPH_R[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
static const uint8_t GLYPH_S[5] = {0x46, 0x49, 0x49, 0x49, 0x31};
static const uint8_t GLYPH_T[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
static const uint8_t GLYPH_U[5] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
static const uint8_t GLYPH_V[5] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
static const uint8_t GLYPH_W[5] = {0x3F, 0x40, 0x38, 0x40, 0x3F};
static const uint8_t GLYPH_X[5] = {0x63, 0x14, 0x08, 0x14, 0x63};
static const uint8_t GLYPH_Y[5] = {0x07, 0x08, 0x70, 0x08, 0x07};
static const uint8_t GLYPH_Z[5] = {0x61, 0x51, 0x49, 0x45, 0x43};

const uint8_t *font5x7_get(char c)
{
    switch (c) {
        case ' ': return GLYPH_SPACE;
        case '.': return GLYPH_DOT;
        case ':': return GLYPH_COLON;
        case '-': return GLYPH_DASH;
        case '%': return GLYPH_PCT;

        case '0': return GLYPH_0;
        case '1': return GLYPH_1;
        case '2': return GLYPH_2;
        case '3': return GLYPH_3;
        case '4': return GLYPH_4;
        case '5': return GLYPH_5;
        case '6': return GLYPH_6;
        case '7': return GLYPH_7;
        case '8': return GLYPH_8;
        case '9': return GLYPH_9;

        case 'A': return GLYPH_A;
        case 'B': return GLYPH_B;
        case 'C': return GLYPH_C;
        case 'D': return GLYPH_D;
        case 'E': return GLYPH_E;
        case 'F': return GLYPH_F;
        case 'G': return GLYPH_G;
        case 'H': return GLYPH_H;
        case 'I': return GLYPH_I;
        case 'J': return GLYPH_J;
        case 'K': return GLYPH_K;
        case 'L': return GLYPH_L;
        case 'M': return GLYPH_M;
        case 'N': return GLYPH_N;
        case 'O': return GLYPH_O;
        case 'P': return GLYPH_P;
        case 'Q': return GLYPH_Q;
        case 'R': return GLYPH_R;
        case 'S': return GLYPH_S;
        case 'T': return GLYPH_T;
        case 'U': return GLYPH_U;
        case 'V': return GLYPH_V;
        case 'W': return GLYPH_W;
        case 'X': return GLYPH_X;
        case 'Y': return GLYPH_Y;
        case 'Z': return GLYPH_Z;

        default: return GLYPH_SPACE;
    }
}
