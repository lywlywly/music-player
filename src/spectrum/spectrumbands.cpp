#include "spectrumbands.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

// IEC 61260-1's base-10 octave ratio G = 10^0.3, so band frequencies are
// 1 kHz times a power of 10.
double atOctaves(double octaves) {
  return 1000.0 * std::pow(10.0, 0.3 * octaves);
}

} // namespace

SpectrumBands SpectrumBands::fractionalOctave(int count) {
  assert(count > 0 && count % 10 == 0);
  const int perOctave = count / 10;
  // Edges every 1/perOctave octave from 5.5 octaves below 1 kHz, so each
  // center is 1 kHz times G^(x/b) for odd b and G^((2x+1)/(2b)) for even b,
  // as the standard defines, and the bands of every count tile the same ten
  // octaves.
  SpectrumBands bands;
  for (int i = 0; i <= count; ++i) {
    bands.edges.push_back(atOctaves(static_cast<double>(i) / perOctave - 5.5));
  }
  for (int i = 0; i < count; ++i) {
    bands.centers.push_back(
        atOctaves((i + 0.5) / perOctave - 5.5)); // The edges' geometric mean.
  }
  return bands;
}

std::string frequencyLabel(double hz) {
  char text[16];
  // ISO 266's preferred frequencies: 1 kHz times 10^(n/10), named by the R10
  // series of preferred numbers.
  static constexpr std::array kPreferred{1.0,  1.25, 1.6, 2.0, 2.5,
                                         3.15, 4.0,  5.0, 6.3, 8.0};
  const double tenths = 10.0 * std::log10(hz / 1000.0);
  const double n = std::round(tenths);
  if (std::abs(tenths - n) < 1e-6) {
    const double decade = std::floor(n / 10.0);
    const double preferred = kPreferred[static_cast<int>(n - 10.0 * decade)] *
                             std::pow(10.0, decade + 3.0);
    if (preferred < 1000.0) {
      std::snprintf(text, sizeof text, "%g", preferred);
    } else {
      std::snprintf(text, sizeof text, "%gK", preferred / 1000.0);
    }
  } else if (hz < 1000.0) {
    std::snprintf(text, sizeof text, "%.0f", hz);
  } else if (hz < 9950.0) { // From 9950, "%.1f" would round to "10.0K".
    std::snprintf(text, sizeof text, "%.1fK", hz / 1000.0);
  } else {
    std::snprintf(text, sizeof text, "%.0fK", hz / 1000.0);
  }
  return text;
}
