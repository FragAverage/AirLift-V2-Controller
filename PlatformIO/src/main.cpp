#include <Arduino.h>

#include "API.h"
#include "CAN.h"
#include "defs.h"
#include "espnow_tx.h"
#include "io.h"
#include "mfl.h"
#include "power_manager.h"
#include "SavvyCAN.h"
#include "tasks.h"

void setup() {
  basicInit();
  canInit();
  mflInit();
  setupWiFi();
  setupApiServer();
  // ESP-NOW rides the soft-AP's radio, so it can only start once WiFi is up.
  espnowTxInit();
  savvyCanInit();

  // Universal reduced-power module: turns WiFi off after the last web client
  // disconnects, scales the CPU 240->80 MHz, releases Bluetooth and kills the
  // onboard LED to cut current draw (and therefore linear-regulator heat).
  // Reconnecting a client (or an ignition power-cycle) brings WiFi back.
  power_config_t pcfg = powerDefaultConfig();
  // The module prints its own [PWR] lines; route them through the master gate
  // so a single enableDebug (and debugPower) controls them too.
  pcfg.verbose = (enableDebug && debugPower);
  powerInit(&pcfg);

  startTasks();
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
