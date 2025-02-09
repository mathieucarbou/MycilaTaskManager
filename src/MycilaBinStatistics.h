// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2023-2024 Mathieu Carbou
 */
#pragma once

#include <stdint.h>

#ifdef MYCILA_JSON_SUPPORT
  #include <ArduinoJson.h>
#endif

namespace Mycila {
  class BinStatistics {
    public:
      // record the number of iterations in each bin.
      // bin sizing is bases on power of 2, so if binCount = 16, we will have 16 bins:
      // bin 0 : 0 <= elapsed < 2^1 (exception for lower bound)
      // bin 1 : 2^1 <= elapsed < 2^2
      // bin 2 : 2^2 <= elapsed < 2^3
      // bin 3 : 2^3 <= elapsed < 2^4
      // ...
      // bin 14 : 2^14 <= elapsed < 2^15
      // bin 15 : 2^15 <= elapsed (exception for upper bound)
      // The unit determines the unit of the elapsed time recorded in the bins.
      // It allows to be more precise depending on the expected task execution durations.
      // unitDivider is the divider to se for the unit: 1 for milliseconds, 1000 for seconds, etc
      explicit BinStatistics(uint8_t binCount, uint32_t unitDivider = 1) : _binCount(binCount), _unitDivider(unitDivider) {
        _bins = new uint16_t[binCount];
        clear();
      }

      ~BinStatistics() { delete[] _bins; }

      // unit divider in milliseconds
      uint32_t unitDivider() const { return _unitDivider; }
      // number of bins
      uint8_t bins() const { return _binCount; }
      // total number of entries
      uint32_t count() const { return _count; }
      // number of entries in a bin
      uint16_t bin(uint8_t index) const { return index < _binCount ? _bins[index] : 0; }

      void clear() {
        _count = 0;
        for (size_t i = 0; i < _binCount; i++)
          _bins[i] = 0;
      }

      void record(uint32_t elapsed) {
        if (_count == UINT32_MAX)
          clear();
        _count++;
        if (!_binCount)
          return;
        uint8_t bin = 0;
        elapsed = elapsed / _unitDivider;
        while (elapsed >>= 1 && bin < _binCount - 1)
          bin++;
        if (_bins[bin] < UINT16_MAX) {
          _bins[bin]++;
        }
      }

#ifdef MYCILA_JSON_SUPPORT
      void toJson(const JsonObject& root) const {
        root["count"] = _count;
        root["unit_divider"] = _unitDivider;
        for (size_t i = 0; i < _binCount; i++)
          root["bins"][i] = _bins[i];
      }
#endif

    private:
      uint8_t _binCount;
      uint32_t _unitDivider;
      uint16_t* _bins;
      uint32_t _count = 0;
  };
} // namespace Mycila
