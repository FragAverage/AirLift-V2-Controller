#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// TCA9554 8-bit I2C GPIO expander (round21 board only). The ST7701 panel's
// reset/CS and the CST820 touch reset all sit on this chip instead of real
// GPIOs — ported from Waveshare's own TCA9554PWR.cpp/.h demo for this board.
// Requires Wire.begin() to already have been called.
// ---------------------------------------------------------------------------
namespace tca9554 {

constexpr uint8_t kAddress   = 0x20;
constexpr uint8_t kInputReg  = 0x00;
constexpr uint8_t kOutputReg = 0x01;
constexpr uint8_t kConfigReg = 0x03;

// All 8 pins to output mode. Output *levels* are left at hardware power-on
// reset default (matches Waveshare's own TCA9554PWR_Init(0x00), which only
// touches the config register) — every pin this board actually uses gets
// explicitly driven by its owner right after, so the POR level never matters.
void begin();

// Drive a single pin (1-8) high/low without disturbing the others.
void setPin(uint8_t pin, bool high);

}  // namespace tca9554
