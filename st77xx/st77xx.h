#ifndef __ST77XX_H__
#define __ST77XX_H__

#ifdef __cplusplus
extern "C" {
#endif

#define ST77XX_80x160_XSTART    25
#define ST77XX_80x160_YSTART    0
#define ST77XX_135x240_XSTART   52
#define ST77XX_135x240_YSTART   40
#define ST77XX_240x240_XSTART   0
#define ST77XX_240x240_YSTART   0

// color modes
#define COLOR_MODE_65K      0x50
#define COLOR_MODE_262K     0x60
#define COLOR_MODE_12BIT    0x03
#define COLOR_MODE_16BIT    0x05
#define COLOR_MODE_18BIT    0x06
#define COLOR_MODE_16M      0x07

// commands
#define ST77XX_NOP     0x00
#define ST77XX_SWRESET 0x01
#define ST77XX_RDDID   0x04
#define ST77XX_RDDST   0x09

#define ST77XX_SLPIN   0x10
#define ST77XX_SLPOUT  0x11
#define ST77XX_PTLON   0x12
#define ST77XX_NORON   0x13

#define ST77XX_INVOFF  0x20
#define ST77XX_INVON   0x21
#define ST77XX_DISPOFF 0x28
#define ST77XX_DISPON  0x29
#define ST77XX_CASET   0x2A
#define ST77XX_RASET   0x2B
#define ST77XX_RAMWR   0x2C
#define ST77XX_RAMRD   0x2E

#define ST77XX_PTLAR   0x30
#define ST77XX_COLMOD  0x3A

#define ST77XX_MADCTL       0x36
#define ST77XX_MADCTL_MY    0x80  // Page Address Order
#define ST77XX_MADCTL_MX    0x40  // Column Address Order
#define ST77XX_MADCTL_MV    0x20  // Page/Column Order
#define ST77XX_MADCTL_ML    0x10  // Line Address Order
#define ST77XX_MADCTL_MH    0x04  // Display Data Latch Order
#define ST77XX_MADCTL_RGB   0x00
#define ST77XX_MADCTL_BGR   0x08

#define ST77XX_MADCTL_R0    0x00
#define ST77XX_MADCTL_R90   0x60
#define ST77XX_MADCTL_R180  0xC0
#define ST77XX_MADCTL_R270  0xA0
extern const uint8_t ST77XX_MADCTL_ROT[4];

#define ST77XX_RDID1   0xDA
#define ST77XX_RDID2   0xDB
#define ST77XX_RDID3   0xDC
#define ST77XX_RDID4   0xDD

// Color definitions
#define	BLACK   0x0000
#define	BLUE    0x001F
#define	RED     0xF800
#define	GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

#ifdef  __cplusplus
}
#endif /*  __cplusplus */

#endif  /*  __ST77XX_H__ */
