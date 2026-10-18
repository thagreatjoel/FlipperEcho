#ifndef IMAGE_CHARGING_LIGHTNING_BITS_H
#define IMAGE_CHARGING_LIGHTNING_BITS_H

#include <Arduino.h>

// 9 x 10 lightning bolt icon (1-bit monochrome, MSB first)
static const unsigned char PROGMEM image_Charging_lightning_bits[] = {
    0x02,0x80,
    0x05,0x00,
    0x0a,0x00,
    0x17,0x80,
    0x21,0x00,
    0x42,0x00,
    0xf4,0x00,
    0x28,0x00,
    0x50,0x00,
    0xa0,0x00
};

#endif // IMAGE_CHARGING_LIGHTNING_BITS_H