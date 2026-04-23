// TFT_eSPI User Setup for ES3C28P (ESP32-S3 + ILI9341V 2.8" IPS)
//
// The esp32s3box board variant defines its own TFT pins (for the Box's LCD).
// We #undef them here before setting our actual ES3C28P pins.
// This file is found first by the compiler (include/ is before the library dir)
// so it overrides TFT_eSPI's own User_Setup.h.

// ── Driver ──
#define ILI9341_DRIVER

// ── Display dimensions ──
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// ── Colour order ──
#define TFT_RGB_ORDER TFT_BGR

// ── ES3C28P pin assignments ──
// Undef anything the esp32s3box variant may have set
#undef TFT_MOSI
#undef TFT_MISO
#undef TFT_CLK
#undef TFT_SCLK
#undef TFT_DC
#undef TFT_CS
#undef TFT_RST
#undef TFT_BL

#define TFT_MOSI  11
#define TFT_SCLK  12
#define TFT_CS    10
#define TFT_MISO  13
#define TFT_DC    46
#define TFT_RST   -1   // RST tied to EN — handled by hardware reset
#define TFT_BL    45
#define TFT_BACKLIGHT_ON HIGH

// ── Force HSPI (SPI3) — required on ESP32-S3 so the SPIClass object
//    and the hardware register writes target the same SPI bus. ──
#define USE_HSPI_PORT

// ── SPI speed ──
#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  16000000

// ── Fonts ──
#define LOAD_GLCD    // Font 1 — 8px bitmap (small labels)
#define LOAD_FONT2   // Font 2 — 16px (status text)
#define LOAD_FONT4   // Font 4 — 26px (large readouts, boot screen)
#define LOAD_GFXFF   // GFX Free Fonts (FreeSans9pt7b, FreeSansBold9pt7b)
