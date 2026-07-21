// TFT_eSPI User Setup — ES3C28P (ESP32-S3 + ILI9341V 2.8" IPS)
// Landscape UI: setRotation(1) in code, physical panel stays 240x320

#define ILI9341_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_RGB_ORDER TFT_BGR

// Undef anything the esp32s3box board variant may have set
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
#define TFT_RST   -1
#define TFT_BL    45
#define TFT_BACKLIGHT_ON HIGH

// Required on ESP32-S3 to target the correct SPI bus
#define USE_HSPI_PORT

#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  16000000

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
