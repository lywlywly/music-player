#include "spectrumbands.h"

#include <cassert>
#include <cmath>
#include <cstdio>

SpectrumBands SpectrumBands::logSpaced(double lowHz, double highHz, int count) {
  assert(count >= 2);
  SpectrumBands bands;
  const double step = std::pow(highHz / lowHz, 1.0 / (count - 1));
  for (int i = 0; i < count; ++i) {
    bands.centers.push_back(lowHz * std::pow(step, i));
  }
  const double halfStep = std::sqrt(step);
  for (int i = 0; i <= count; ++i) {
    bands.edges.push_back(lowHz * std::pow(step, i) / halfStep);
  }
  return bands;
}

std::string frequencyLabel(double hz) {
  char text[16];
  if (hz < 1000.0) {
    std::snprintf(text, sizeof text, "%.0f", hz);
  } else if (hz < 9950.0) { // From 9950, "%.1f" would round to "10.0K".
    std::snprintf(text, sizeof text, "%.1fK", hz / 1000.0);
  } else {
    std::snprintf(text, sizeof text, "%.0fK", hz / 1000.0);
  }
  return text;
}
