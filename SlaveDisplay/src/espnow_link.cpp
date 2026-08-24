#include "espnow_link.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "config.h"

namespace espnow {
namespace {

// Broadcast: no pairing, no knowledge of the master's MAC needed either way.
const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Written from the Wi-Fi task in the recv callback, read from loop() — guarded
// by a spinlock so a half-updated struct can never be rendered.
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool  newData      = false;
AirLiftData    receivedData = {};
volatile uint32_t lastRxMs  = 0;

volatile bool  newButtons      = false;
AirLiftButtons receivedButtons = {};

void handlePacket(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;

  if (len == (int)sizeof(AirLiftData)) {
    portENTER_CRITICAL(&mux);
    memcpy(&receivedData, data, sizeof(AirLiftData));
    newData  = true;
    lastRxMs = millis();
    portEXIT_CRITICAL(&mux);
    return;
  }

  if (len == (int)sizeof(AirLiftButtons)) {
    portENTER_CRITICAL(&mux);
    memcpy(&receivedButtons, data, sizeof(AirLiftButtons));
    newButtons = true;
    portEXIT_CRITICAL(&mux);
    return;
  }

  Serial.printf("[NOW] dropped packet: %d bytes (expected %u or %u)\n", len,
                (unsigned)sizeof(AirLiftData), (unsigned)sizeof(AirLiftButtons));
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

  // Lower TX power -- this board and the master sit inches apart in the same
  // cluster, so full power is wasted current (and board heat) for no range
  // benefit. Same reasoning and same value the master's power_manager
  // already uses for its own "in-car close range" TX power reduction
  // (PlatformIO/src/power_manager.cpp's wifiTxPowerActive). Deliberately NOT
  // touching WiFi.setSleep() here -- modem sleep trades latency/dropped
  // packets for power savings, and this board's whole job is a low-latency
  // live gauge reading over a broadcast link with no retransmission, so
  // that trade isn't worth it without it actually being asked for.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

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

  // Needed for sendCommand() — esp_now_send() requires a registered peer even
  // for broadcast.
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kBroadcastMac, sizeof(kBroadcastMac));
  peer.channel = 0;   // "whatever channel we're already parked on"
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[NOW] add_peer FAILED — sendCommand() will not work");
  }

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

bool takeButtons(AirLiftButtons& out) {
  bool got = false;
  portENTER_CRITICAL(&mux);
  if (newButtons) {
    out        = receivedButtons;
    newButtons = false;
    got        = true;
  }
  portEXIT_CRITICAL(&mux);
  return got;
}

void sendCommand(uint8_t cmd, uint8_t param) {
  AirLiftCommand c;
  c.cmd   = cmd;
  c.param = param;
  esp_now_send(kBroadcastMac, (const uint8_t*)&c, sizeof(c));
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
