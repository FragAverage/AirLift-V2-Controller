#include "espnow_link.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "config.h"

namespace espnow {
namespace {

// Written from the Wi-Fi task in the recv callback, read from loop() — guarded
// by a spinlock so a half-updated struct can never be rendered.
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool  newData      = false;
AirLiftData    receivedData = {};
volatile uint32_t lastRxMs  = 0;

void handlePacket(const uint8_t* mac, const uint8_t* data, int len) {
  if (len != (int)sizeof(AirLiftData)) {
    Serial.printf("[NOW] dropped packet: %d bytes, expected %u\n",
                  len, (unsigned)sizeof(AirLiftData));
    return;
  }

  portENTER_CRITICAL(&mux);
  memcpy(&receivedData, data, sizeof(AirLiftData));
  newData  = true;
  lastRxMs = millis();
  portEXIT_CRITICAL(&mux);

  (void)mac;
}

// Arduino-ESP32 3.x (IDF 5) changed the callback signature; support both.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  handlePacket(info ? info->src_addr : nullptr, data, len);
}
#else
void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  handlePacket(mac, data, len);
}
#endif

}  // namespace

void begin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);  // make sure we are not chasing an AP

  // ESP-NOW only works between radios parked on the same channel. Nothing here
  // ever associates, so pin the channel explicitly.
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  Serial.printf("[NOW] STA MAC %s, channel %d\n",
                WiFi.macAddress().c_str(), ESPNOW_WIFI_CHANNEL);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[NOW] esp_now_init() FAILED — restarting");
    delay(1000);
    ESP.restart();
  }

  esp_now_register_recv_cb(onRecv);
  Serial.println("[NOW] receiver ready");
}

bool take(AirLiftData& out) {
  bool got = false;
  portENTER_CRITICAL(&mux);
  if (newData) {
    out     = receivedData;
    newData = false;
    got     = true;
  }
  portEXIT_CRITICAL(&mux);
  return got;
}

uint32_t lastPacketMs() {
  portENTER_CRITICAL(&mux);
  uint32_t t = lastRxMs;
  portEXIT_CRITICAL(&mux);
  return t;
}

bool alive() {
  uint32_t t = lastPacketMs();
  return t != 0 && (millis() - t) < LINK_TIMEOUT_MS;
}

}  // namespace espnow
