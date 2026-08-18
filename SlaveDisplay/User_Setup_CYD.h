// ---------------------------------------------------------------------------
// TFT_eSPI setup for the ESP32-2432S028R (Cheap Yellow Display).
//
// *** PlatformIO users do NOT need this file. ***
// platformio.ini already passes every one of these values as -D build flags
// (with USER_SETUP_LOADED=1), which is why the library folder never has to be
// hand-edited. This copy exists for Arduino IDE builds: replace the contents of
//   <Arduino>/libraries/TFT_eSPI/User_Setup.h
// with this file.
//
// Note there is deliberately no TOUCH_CS here. The XPT2046 on this board sits
// on its own VSPI bus and is driven by XPT2046_Touchscreen — handing TFT_eSPI a
// TOUCH_CS would make it talk to the touch chip over the display's HSPI bus.
// ---------------------------------------------------------------------------
#define ILI9341_DRIVER

#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

// Keep the panel on HSPI so VSPI stays free for touch.
#define USE_HSPI_PORT

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define SMOOTH_FONT

#define SPI_FREQUENCY       55000000
#define SPI_READ_FREQUENCY  20000000
