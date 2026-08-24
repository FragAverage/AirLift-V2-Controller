// Standalone bench CAN sniffer — confirms whether a bus is actually present
// and readable on a given pair of wires before trusting any multimeter
// reading or wiring anything permanent.
//
// Not part of the AirLift firmware. Uses the ESP32's built-in TWAI (CAN)
// controller through an external transceiver (TJA1050 or similar):
//   ESP32 GPIO17 (TX) -> transceiver TXD
//   ESP32 GPIO16 (RX) -> transceiver RXD
//   Transceiver CANH/CANL -> the bus under test
//
// LISTEN_ONLY mode, always. This is a bench probe on a bus of unknown
// origin (possibly a real vehicle's live network) — the driver must never
// transmit or emit ACK bits onto it. Don't change this to NORMAL mode here.
//
// Baud rate: starts at 500 kbit/s (typical automotive powertrain CAN). If
// you see nothing but climbing error counters below, try
// TWAI_TIMING_CONFIG_100KBITS() or TWAI_TIMING_CONFIG_125KBITS() instead —
// wrong bit rate looks exactly like "bus present but garbage", not silence.

#include <Arduino.h>
#include <driver/twai.h>

constexpr gpio_num_t kCanTxPin = GPIO_NUM_17;
constexpr gpio_num_t kCanRxPin = GPIO_NUM_16;

uint32_t framesSeen = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("[CAN] bench sniffer starting (LISTEN_ONLY, 500 kbit/s)");

  twai_general_config_t g =
      TWAI_GENERAL_CONFIG_DEFAULT(kCanTxPin, kCanRxPin, TWAI_MODE_LISTEN_ONLY);
  twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g, &t, &f) != ESP_OK) {
    Serial.println("[CAN] driver_install FAILED — check pins/wiring");
    return;
  }
  if (twai_start() != ESP_OK) {
    Serial.println("[CAN] twai_start FAILED");
    return;
  }
  Serial.println("[CAN] started, waiting for frames...");
}

void printFrame(const twai_message_t& f) {
  Serial.printf("[%8lu] id=0x%03lX%s dlc=%u data=",
                (unsigned long)millis(), (unsigned long)f.identifier,
                f.extd ? " (ext)" : "", (unsigned)f.data_length_code);
  for (int i = 0; i < f.data_length_code; i++) {
    Serial.printf("%02X ", f.data[i]);
  }
  Serial.println();
}

void loop() {
  twai_message_t frame;
  while (twai_receive(&frame, 0) == ESP_OK) {
    printFrame(frame);
    framesSeen++;
  }

  // Once a second: frame count + bus state/error counters. If frames stay at
  // 0 with the bus state RUNNING and error counters at 0, nothing is being
  // received at all (wrong pins, no signal, or the bus genuinely isn't CAN).
  // Climbing error counters with 0 good frames usually means wrong bit rate,
  // CANH/CANL swapped, or missing termination rather than "no bus".
  static uint32_t lastStatusMs = 0;
  const uint32_t  now          = millis();
  if (now - lastStatusMs >= 1000) {
    lastStatusMs = now;
    twai_status_info_t status = {};
    if (twai_get_status_info(&status) == ESP_OK) {
      const char* stateStr =
          status.state == TWAI_STATE_RUNNING    ? "RUNNING"
          : status.state == TWAI_STATE_BUS_OFF  ? "BUS_OFF"
          : status.state == TWAI_STATE_STOPPED  ? "STOPPED"
          : status.state == TWAI_STATE_RECOVERING ? "RECOVERING"
                                                   : "?";
      Serial.printf("[CAN] status: state=%s frames=%lu tx_err=%u rx_err=%u\n",
                    stateStr, (unsigned long)framesSeen,
                    (unsigned)status.tx_error_counter,
                    (unsigned)status.rx_error_counter);
    }
  }
}
