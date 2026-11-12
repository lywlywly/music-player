#ifndef SPECTRUMANALYZER_H
#define SPECTRUMANALYZER_H

#include "spectrumbands.h"
#include <complex>
#include <span>
#include <vector>

// Turns a window of mono samples into one level per band, in dB relative to
// a full-scale sine, clamped to [kFloorDb, 0]. See docs/spectrum.md.
class SpectrumAnalyzer {
public:
  static constexpr float kFloorDb = -60.0f;

  // fftSize must be a power of two.
  explicit SpectrumAnalyzer(SpectrumBands bands, int fftSize = 4096);

  const SpectrumBands &bands() const { return bands_; }
  int fftSize() const { return static_cast<int>(window_.size()); }

  // samples holds fftSize() samples in [-1, 1].
  std::vector<float> analyze(std::span<const float> samples, int sampleRate);

private:
  SpectrumBands bands_;
  std::vector<float> window_;
  float windowSquareSum_ = 0.0f;
  std::vector<std::complex<float>> buffer_;
};

#endif // SPECTRUMANALYZER_H
