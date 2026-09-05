// lib/hardware/board_config.h
// Board/pin abstraction — adjust per hardware revision.
#pragma once

// Status LED (active-low when the anode is tied to VCC).
#define BOARD_LED_PIN           2
#define BOARD_LED_ACTIVE_LOW    1

// Soil sensor ADC inputs (ADC1 channels, input-only pins on most devkits).
#define BOARD_PH_ADC_PIN        34
#define BOARD_LIGHT_ADC_PIN     35
#define BOARD_MOIST_ADC_PIN     33

// Optional sensor power switch.
#define BOARD_SENSOR_POWER_PIN  25

// Setup-mode GPIO input. Hold LOW at boot to enter SETUP (batch-config) mode
// instead of the normal M/S operation. NOTE: on classic ESP32 modules GPIO 6-11
// are tied to the SPI flash and are NOT usable — use an ESP32-S3 or pick
// another free input pin on your board.
#define BOARD_SETUP_PIN         6
#define BOARD_SETUP_ACTIVE_LOW  1   // 1 = LOW triggers SETUP mode

// ADC settings.
#define BOARD_ADC_RESOLUTION    4095
#define BOARD_ADC_ATTENUATION   11    // 11 dB ~= 0..3.3 V full scale

// Sensor model/type id reported in DP_TLM_OFF_SENSORTYPE.
#define BOARD_SENSOR_TYPE_ID    1
