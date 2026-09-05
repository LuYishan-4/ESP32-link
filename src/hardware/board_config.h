// lib/hardware/board_config.h
// ESP32-C3 SuperMini board/pin abstraction
#pragma once

// ============================================================
// Status LED
// ============================================================
// ESP32-C3 SuperMini 常見板載 LED 接在 GPIO8，通常為 Active-Low。
// 注意：GPIO8 是 strapping pin，因此不要在開機時強制拉 LOW。
#define BOARD_LED_PIN           8
#define BOARD_LED_ACTIVE_LOW    1


// ============================================================
// Analog Sensors
// ============================================================
// ESP32-C3 ADC1:
// GPIO0 -> ADC1_CH0
// GPIO1 -> ADC1_CH1
// GPIO2 -> ADC1_CH2
// GPIO3 -> ADC1_CH3
// GPIO4 -> ADC1_CH4
//
// 優先使用 ADC1，避免 WiFi + ADC2 的相容性問題。

#define BOARD_PH_ADC_PIN        0   // ADC1_CH0
#define BOARD_LIGHT_ADC_PIN     1   // ADC1_CH1
#define BOARD_MOIST_ADC_PIN     3   // ADC1_CH3


// ============================================================
// Sensor Power
// ============================================================
// GPIO10 作為感測器電源控制。
// GPIO10 是一般 GPIO，適合拿來做數位輸出。
#define BOARD_SENSOR_POWER_PIN  10


// ============================================================
// Setup-mode GPIO
// ============================================================
// 開機時把 GPIO6 拉到 LOW（接地）即進入 SETUP(批量設定) 模式，
// 否則進入一般 M/S 操作。GPIO6 在 ESP32-C3 是一般可用的輸入腳。
#define BOARD_SETUP_PIN         6
#define BOARD_SETUP_ACTIVE_LOW  1   // 1 = LOW triggers SETUP mode


// ============================================================
// ADC Settings
// ============================================================

#define BOARD_ADC_RESOLUTION    4095

// ESP32-C3 Arduino ADC attenuation
// 11 dB 可用於較高的輸入電壓範圍。
// 實際可測範圍與線性區域仍應以實際 C3 ADC 特性與校準為準。
#define BOARD_ADC_ATTENUATION   11


// ============================================================
// Sensor Type
// ============================================================

#define BOARD_SENSOR_TYPE_ID    1
