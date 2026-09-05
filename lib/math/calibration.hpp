// lib/math/calibration.hpp
// Soil-sensor calibration curves (raw ADC -> pH / %RH / lux).
// Piecewise-linear; tune the point tables per sensor lot.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "unit_convert.hpp"

namespace math {

struct CalibrationPoint {
  uint16_t raw;
  float    eng;
};

// Linear interpolation through a sorted (by `raw`) point table.
inline float calibrateCurve(uint16_t raw, const CalibrationPoint* pts, size_t count) {
  if (count == 0 || pts == nullptr) return 0.0f;
  if (raw <= pts[0].raw) return pts[0].eng;
  if (raw >= pts[count - 1].raw) return pts[count - 1].eng;
  for (size_t i = 1; i < count; ++i) {
    if (raw <= pts[i].raw) {
      const CalibrationPoint& a = pts[i - 1];
      const CalibrationPoint& b = pts[i];
      if (b.raw == a.raw) return a.eng;
      return a.eng + (b.eng - a.eng) * (float)(raw - a.raw) / (float)(b.raw - a.raw);
    }
  }
  return pts[count - 1].eng;
}

// Soil pH curve (raw ADC -> pH). Default assumes 0..4095 -> 14.0..0.0.
inline float adcToPh(uint16_t raw) {
  static const CalibrationPoint curve[] = {
    {    0, 14.0f },   // dry / alkaline end (probe dependent)
    { 2048,  7.0f },
    { 4095,  0.0f }
  };
  return calibrateCurve(raw, curve, sizeof(curve) / sizeof(curve[0]));
}

// Soil moisture % (raw ADC -> %). Resistive probe: wet = low resistance = low raw.
inline float adcToMoisturePercent(uint16_t raw) {
  static const CalibrationPoint curve[] = {
    {    0, 100.0f },
    { 4095,   0.0f }
  };
  return calibrateCurve(raw, curve, 2);
}

// Light level in lux (raw ADC -> lux), approximate for an LDR/PT550 divider.
inline float adcToLux(uint16_t raw) {
  static const CalibrationPoint curve[] = {
    {    0,      0.0f },
    { 4095, 100000.0f }
  };
  return calibrateCurve(raw, curve, 2);
}

} // namespace math
