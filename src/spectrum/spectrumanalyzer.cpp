#include "spectrumanalyzer.h"

#include "fft.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

SpectrumAnalyzer::SpectrumAnalyzer(SpectrumBands bands, int fftSize)
    : bands_(std::move(bands)), window_(fftSize), buffer_(fftSize) {
  // Hann window: without it, the cut at the window's ends spreads a pure tone
  // over every bin.
  for (int n = 0; n < fftSize; ++n) {
    window_[n] = static_cast<float>(
        0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * n / (fftSize - 1)));
    windowSquareSum_ += window_[n] * window_[n];
  }
}

std::vector<float> SpectrumAnalyzer::analyze(std::span<const float> samples,
                                             int sampleRate) {
  const int n = fftSize();
  assert(static_cast<int>(samples.size()) == n);
  for (int i = 0; i < n; ++i) {
    buffer_[i] = {samples[i] * window_[i], 0.0f};
  }
  fft(buffer_);

  // Power of bin k, scaled so that the bins of a sine of amplitude A add up
  // to A^2 (Parseval: the positive bins of a windowed sine hold
  // n * sum(w^2) * A^2 / 4). A full-scale sine then reads 0 dB, wherever it
  // falls between bins.
  const float scale = 4.0f / (static_cast<float>(n) * windowSquareSum_);
  const auto power = [&](int k) { return std::norm(buffer_[k]) * scale; };
  const double binHz = static_cast<double>(sampleRate) / n;
  const int lastBin = n / 2;

  std::vector<float> levels;
  levels.reserve(bands_.count());
  for (int b = 0; b < bands_.count(); ++b) {
    float sum = 0.0f;
    if (bands_.centers[b] < sampleRate / 2.0) {
      const int first =
          std::max(1, static_cast<int>(std::ceil(bands_.edges[b] / binHz)));
      const int last = std::min(
          lastBin,
          static_cast<int>(std::ceil(bands_.edges[b + 1] / binHz)) - 1);
      if (first <= last) {
        for (int k = first; k <= last; ++k) {
          sum += power(k);
        }
      } else {
        // A low band narrower than a bin: the bin nearest its center.
        sum = power(
            std::clamp(static_cast<int>(std::lround(bands_.centers[b] / binHz)),
                       1, lastBin));
      }
    }
    const float db = sum > 0.0f ? 10.0f * std::log10(sum) : kFloorDb;
    levels.push_back(std::clamp(db, kFloorDb, 0.0f));
  }
  return levels;
}
