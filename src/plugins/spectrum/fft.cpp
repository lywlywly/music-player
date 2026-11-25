#include "fft.h"

#include <cassert>
#include <cmath>
#include <numbers>
#include <utility>

void fft(std::vector<std::complex<float>> &data) {
  const size_t n = data.size();
  assert((n & (n - 1)) == 0);
  // Bit-reversal permutation, so the butterflies below work in place.
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; (j & bit) != 0; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      std::swap(data[i], data[j]);
    }
  }
  // Each pass merges pairs of transforms of length len / 2.
  for (size_t len = 2; len <= n; len <<= 1) {
    const double angle = -2.0 * std::numbers::pi / static_cast<double>(len);
    const size_t half = len / 2;
    for (size_t k = 0; k < half; ++k) {
      const std::complex<float> w(
          static_cast<float>(std::cos(angle * static_cast<double>(k))),
          static_cast<float>(std::sin(angle * static_cast<double>(k))));
      for (size_t i = k; i < n; i += len) {
        const std::complex<float> u = data[i];
        const std::complex<float> v = data[i + half] * w;
        data[i] = u + v;
        data[i + half] = u - v;
      }
    }
  }
}
