# ES3C28P / ES3N28P — 2.8" IPS ESP32-S3 Display Module

> **Source:** https://www.lcdwiki.com/2.8inch_ESP32-S3_Display
> **SKU:** ES3C28P (with touch) / ES3N28P (without touch)
> **Last updated from wiki:** 2026-03-16

---

## Product Features

- ESP32-S3 main controller with extensive dev resources
- 2.8" IPS TFT, 240×320, up to 262K colors (RGB666)
- Rich peripheral interfaces (I2C, UART, etc.)
- External speaker support for audio playback
- Onboard MEMS microphone for audio input
- RGB three-color LED (WS2812B, single-wire)
- Capacitive touch screen (ES3C28P variant)
- USB Type-C for programming and power
- MicroSD card slot (SDIO, 4-bit)
- External lithium battery support (3.7V LiPo)
- Onboard battery charging management circuit (TP4054)
- Supports "Xiaozhi" AI voice chat

---

## ESP32-S3 Parameters

| Item              | Value                                                        |
| ----------------- | ------------------------------------------------------------ |
| Module            | ESP32-S3                                                     |
| CPU               | Xtensa LX7 32-bit dual-core                                 |
| Max Clock         | 240 MHz                                                      |
| Memory            | 384KB ROM + 512KB SRAM + 16KB RTC SRAM + 8MB OPI PSRAM + 16MB SPI Flash (N16R8) |
| WiFi              | 2.4 GHz, 802.11 b/g/n                                       |
| Bluetooth         | V5.0 BR/EDR + BLE                                           |
| Operating Voltage | 3.0–3.6 V                                                   |

---

## Screen Parameters

| Item                  | Value                     |
| --------------------- | ------------------------- |
| Size                  | 2.8 inch                  |
| Type                  | IPS TFT                   |
| Resolution            | 240 × RGB × 320 pixels   |
| Active Area           | 43.20 (W) × 57.60 (H) mm |
| Color Depth           | Max 262K (RGB666), Typ 65K (RGB565) |
| Driver IC             | ILI9341V                  |
| Interface             | 4-Line SPI                |
| Pixel Size            | 0.153 × 0.153 mm         |
| Viewing Angle         | All (IPS)                 |
| Luminance (typ)       | 280 cd/m²                 |
| Backlight             | 4× White LED              |
| Operating Temperature | -30 to 80 °C              |
| Storage Temperature   | -30 to 80 °C              |

---

## Touch Screen Parameters

| Item                  | Value                         |
| --------------------- | ----------------------------- |
| Size                  | 2.8 inch                      |
| Type                  | Capacitive                    |
| Valid Area            | 240 × 320 pixels             |
| Visual Area           | 45.20 (W) × 59.45 (H) mm    |
| Driver IC             | D-FT6336G                     |
| Interface             | I2C                           |
| Operating Temperature | -30 to 80 °C                  |
| Storage Temperature   | -30 to 80 °C                  |

---

## Physical Size

| Variant     | Dimensions (W × H × D)         |
| ----------- | ------------------------------- |
| With touch  | 50.00 × 86.00 × 10.60 mm      |
| No touch    | 50.00 × 86.00 × 9.10 mm       |
| LCD panel   | 50.00 × 69.20 × 2.3 mm (±0.1) |
| Touch panel | 50.00 × 69.20 × 1.20 mm (±0.1)|

---

## Battery Charging Parameters

| Item                       | Value                                |
| -------------------------- | ------------------------------------ |
| Charging IC                | TP4054                               |
| Charging Voltage           | 4.2–6.5 V (typical 5 V)             |
| Charging Current           | Max 500 mA, actual module ~290 mA   |
| Charging Saturation Voltage| 4.24 V                               |
| Charging Temperature       | 62 °C                                |
| Battery Spec               | 3.7 V polymer lithium               |

---

## Electrical Parameters

| Item                            | Value                                          |
| ------------------------------- | ---------------------------------------------- |
| Operating Voltage               | 5.0 V (USB)                                   |
| Backlight Current               | 79 mA                                          |
| Backlight Brightness (touch)    | 230 cd/m²                                      |
| Backlight Brightness (no touch) | 270 cd/m²                                      |
| Total Current (reset)           | ~0 mA                                          |
| Total Current (display only)    | 140 mA                                         |
| Total Current (display+speaker+charging) | 560 mA                                |
| Power (display only)            | 0.7 W                                          |
| Power (display+speaker+charging)| 2.8 W                                          |

---

## Other

| Item             | Value                |
| ---------------- | -------------------- |
| Power Interface  | USB Type-C           |
| Weight (ES3C28P) | 111 g                |
| Weight (ES3N28P) | 100 g                |

---

## Pin Assignments

### LCD (ILI9341V, 4-Line SPI)

| Function       | GPIO | Notes                                              |
| -------------- | ---- | -------------------------------------------------- |
| CS             | IO10 | Chip select, active LOW                            |
| DC             | IO46 | Data/Command select (HIGH=data, LOW=command)       |
| SCK (SPI CLK)  | IO12 | SPI bus clock                                      |
| MOSI (SPI TX)  | IO11 | SPI bus write data                                 |
| MISO (SPI RX)  | IO13 | SPI bus read data                                  |
| RST            | RST  | Shared with ESP32-S3 reset (EN pin), active LOW    |
| Backlight      | IO45 | HIGH = on, LOW = off                               |

### Touch Screen (D-FT6336G, I2C)

| Function  | GPIO | Notes                                  |
| --------- | ---- | -------------------------------------- |
| SDA       | IO16 | I2C data (shared with external I2C)    |
| SCL       | IO15 | I2C clock (shared with external I2C)   |
| RST       | IO18 | Touch reset, active LOW                |
| INT       | IO17 | Touch interrupt, LOW on touch event    |

### Audio (ES8311 codec + FM8002E amplifier)

| Function      | GPIO | Notes                                          |
| ------------- | ---- | ---------------------------------------------- |
| AUDIO_EN      | IO1  | Audio output enable: LOW=enable, HIGH=disable  |
| I2S MCLK      | IO4  | Master clock                                   |
| I2S BCLK      | IO5  | Bit clock                                      |
| I2S DOUT      | IO6  | Data output (to speaker/codec)                 |
| I2S WS (LRCK) | IO7  | Word select (HIGH=right, LOW=left)             |
| I2S DIN       | IO8  | Data input (from microphone/codec)             |

> **Note:** The wiki pin mapping differs from the board schematic values previously confirmed (BCLK=GPIO10, WS=GPIO12, DOUT=GPIO11, DIN=GPIO13, MCLK=GPIO9, AUDIO_EN=GPIO46). There may be different hardware revisions. **Always verify against your specific board's schematic.**

### MicroSD Card (SDIO, 4-bit)

| Function | GPIO        | Notes             |
| -------- | ----------- | ----------------- |
| CLK      | IO38        | SDIO clock        |
| CMD      | IO40        | SDIO command      |
| DATA0    | IO39        | SDIO data line 0  |
| DATA1    | IO41        | SDIO data line 1  |
| DATA2    | IO48        | SDIO data line 2  |
| DATA3    | IO47        | SDIO data line 3  |

### RGB LED (WS2812B)

| Function | GPIO |
| -------- | ---- |
| Data     | IO42 |

### Battery ADC

| Function    | GPIO | Notes                                   |
| ----------- | ---- | --------------------------------------- |
| Battery ADC | IO9  | Via voltage divider (read battery level)|

### UART0

| Function | GPIO  |
| -------- | ----- |
| RXD0     | IO43  |
| TXD0     | IO44  |

### Buttons

| Button | GPIO / Pin | Notes                                               |
| ------ | ---------- | --------------------------------------------------- |
| BOOT   | IO0        | Hold during power-on → download mode; else user key |
| RESET  | EN         | Low-level reset (shared with LCD reset)             |

### Expansion Pins (1.25mm 4P socket)

| GPIO |
| ---- |
| IO2  |
| IO3  |
| IO14 |
| IO21 |

### External I2C (1.25mm 4P socket, shared with touch)

| Function | GPIO |
| -------- | ---- |
| SDA      | IO16 |
| SCL      | IO15 |

---

## Onboard ICs & Datasheets

| Component            | IC / Part          | Role                           |
| -------------------- | ------------------ | ------------------------------ |
| MCU                  | ESP32-S3 (N16R8)   | Main controller                |
| LCD Driver           | ILI9341V           | TFT display controller         |
| Touch Controller     | D-FT6336G          | Capacitive touch (I2C)         |
| Audio Codec          | ES8311             | I2S audio codec (mic + speaker)|
| Audio Amplifier      | FM8002E            | Class-D amp (max 1.5W@8Ω, 2W@4Ω) |
| MEMS Microphone      | LMA2718B381-OA7    | Analog MEMS mic                |
| Battery Charger      | TP4054             | Li-ion charge management       |
| Voltage Regulator    | ME6217             | LDO regulator                  |
| RGB LED              | WS2812B-V5         | Addressable RGB (single-wire)  |
| USB-UART Bridge      | (via Type-C)       | Auto-download circuit          |

---

## Interfaces Summary

| Interface        | Connector       | Notes                                            |
| ---------------- | --------------- | ------------------------------------------------ |
| USB Type-C       | On-board        | Power + programming (auto-download)              |
| MicroSD          | On-board slot   | SDIO 4-bit bus                                   |
| UART             | 1.25mm 4P       | Debug/download/comms (needs USB-UART adapter)    |
| Battery          | 1.25mm 2P       | 3.7V LiPo, mind polarity                        |
| Speaker          | 1.25mm 2P       | Max 1.5W@8Ω or 2W@4Ω                            |
| I2C              | 1.25mm 4P       | External I2C (shared bus with touch)             |
| Expansion GPIO   | 1.25mm 4P       | IO2, IO3, IO14, IO21                             |

---

## Development Resources

### Frameworks Supported
- Arduino IDE
- MicroPython
- ESP-IDF (with LVGL support)

### Key Download Links (from wiki)
- Data pack: `2.8inch_IPS_ESP32-S3_ILI9341V_ES3C28P_ES3N28P_V1.0`
- Quick start zip: `2.8inch_IPS_ESP32-S3_ES3C28P_ES3N28P_Quick_Start`
- Schematic PDF: `2.8inch_ESP32-S3_Display_Schematic.pdf`
- I/O allocation table (Excel): `ESP32-S3芯片IO资源分配表.xlsx`
- ILI9341 init code: `ILI9341V_Init.txt`
- AD library (footprint): `2.8inch_IPS_ESP32-S3_Display_AD封装库.zip`
- 3D models: `ES3C28P_3D.zip` / `ES3N28P_3D.zip`

### Tools
- Flash Download Tool (Espressif)
- JPGCompact V5.0
- TCP/UDP test tool
- Serial debugging assistant
- Network debugging assistant
- ESPTouch v2.0.0 (Android APK)
- PCtoLCD2002
- Image2Lcd
