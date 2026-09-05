// lib/math/unit_convert.hpp
// Raw ADC <-> engineering unit conversions. Header-only, no .cpp.
#pragma once

#include <stdint.h>

namespace math {

// Raw ADC -> voltage, for a given reference (mV) and ADC resolution.
inline float adcToVoltage(uint16_t adc, uint16_t resolution = 4095, float vrefMv = 3300.0f) {
  if (resolution == 0) return 0.0f;
  return (float)adc * vrefMv / (float)resolution;
}

inline float adcToMilliVolt(uint16_t adc, uint16_t resolution = 4095, float vrefMv = 3300.0f) {
  return adcToVoltage(adc, resolution, vrefMv);
}

// Linear map of a value from one range to another (float-safe for small types).
template <typename T>
inline T mapRange(T x, T inMin, T inMax, T outMin, T outMax) {
  if (inMax == inMin) return outMin;
  float f = ((float)(x - inMin) * (float)(outMax - outMin)) / (float)(inMax - inMin);
  return (T)(outMin + f);
}

// Clamp helper.
template <typename T>
inline T clamp(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace math
