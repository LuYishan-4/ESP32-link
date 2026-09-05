// lib/hardware/gpio_manager.cpp
#include "gpio_manager.h"
#include "board_config.h"
#include <Arduino.h>

static uint8_t  s_blinkSteps = 0;
static uint16_t s_blinkOn    = 120;
static uint16_t s_blinkOff   = 120;
static uint32_t s_lastToggle = 0;
static bool     s_ledState   = false;

static void writeLed(bool on) {
  digitalWrite(BOARD_LED_PIN, (on == (BOARD_LED_ACTIVE_LOW != 0)) ? LOW : HIGH);
}

void GpioManager::begin() {
  pinMode(BOARD_LED_PIN, OUTPUT);
  pinMode(BOARD_SENSOR_POWER_PIN, OUTPUT);
  writeLed(false);
  setSensorPower(true);
}

void GpioManager::setLed(bool on) {
  s_blinkSteps = 0;
  s_ledState   = on;
  writeLed(on);
}

void GpioManager::blink(uint8_t times, uint16_t onMs, uint16_t offMs) {
  if (times == 0) { setLed(false); return; }
  s_blinkSteps = (uint16_t)times * 2;
  s_blinkOn    = onMs;
  s_blinkOff   = offMs;
  s_lastToggle = millis();
  s_ledState   = false;
  writeLed(false);
}

void GpioManager::loop() {
  if (s_blinkSteps == 0) return;
  uint32_t now    = millis();
  uint16_t period = s_ledState ? s_blinkOn : s_blinkOff;
  if ((uint32_t)(now - s_lastToggle) >= period) {
    s_lastToggle = now;
    s_ledState   = !s_ledState;
    writeLed(s_ledState);
    s_blinkSteps--;
  }
}

void GpioManager::setSensorPower(bool on) {
  digitalWrite(BOARD_SENSOR_POWER_PIN, on ? HIGH : LOW);
}

bool GpioManager::setupModeRequested() {
  pinMode(BOARD_SETUP_PIN, INPUT_PULLUP);
  delay(60);                       // let the pin settle / allow the user to hold it
  int v = digitalRead(BOARD_SETUP_PIN);
  bool setup = BOARD_SETUP_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
  Serial.printf("[boot] setup pin GPIO%d=%d -> %s\n",
                BOARD_SETUP_PIN, v, setup ? "SETUP MODE" : "NORMAL MODE");
  return setup;
}
