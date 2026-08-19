#include "tca9554.h"

#include <Wire.h>

namespace tca9554 {
namespace {

uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(kAddress);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(kAddress, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

void writeReg(uint8_t reg, uint8_t data) {
  Wire.beginTransmission(kAddress);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

}  // namespace

void begin() {
  writeReg(kConfigReg, 0x00);  // all 8 pins -> output mode
}

void setPin(uint8_t pin, bool high) {
  const uint8_t bit = 1u << (pin - 1);
  const uint8_t current = readReg(kOutputReg);
  writeReg(kOutputReg, high ? (current | bit) : (current & ~bit));
}

}  // namespace tca9554
