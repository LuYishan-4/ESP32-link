// lib/math/filters.hpp
// Moving-average / smoothing filters for noisy sensor reads. Header-only.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace math {

// Simple moving-average over a fixed-size ring buffer.
template <typename T, size_t N>
class MovingAverage {
public:
  void add(T value) {
    _sum -= _buf[_idx];
    _buf[_idx] = value;
    _sum += value;
    _idx = (_idx + 1) % N;
    if (_count < N) _count++;
  }

  T get() const {
    return _count ? (T)(_sum / _count) : T(0);
  }

  void reset() {
    _idx = 0;
    _count = 0;
    _sum = T(0);
    for (size_t i = 0; i < N; ++i) _buf[i] = T(0);
  }

private:
  T      _buf[N];
  size_t _idx   = 0;
  size_t _count = 0;
  T      _sum   = T(0);
};

// Exponential moving average — cheaper than the ring buffer, no history.
template <typename T>
class ExponentialFilter {
public:
  explicit ExponentialFilter(float alpha = 0.2f) : _alpha(alpha) {}

  void add(T value) {
    if (!_init) {
      _value = value;
      _init  = true;
    } else {
      _value = (T)(_alpha * (float)value + (1.0f - _alpha) * (float)_value);
    }
  }

  T get() const { return _value; }

  void reset(T value = T(0)) {
    _value = value;
    _init  = false;
  }

private:
  float _alpha;
  T     _value = T(0);
  bool  _init  = false;
};

} // namespace math
