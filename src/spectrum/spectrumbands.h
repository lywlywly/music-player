#ifndef SPECTRUMBANDS_H
#define SPECTRUMBANDS_H

#include <string>
#include <vector>

// Frequency bands of the spectrum, log-spaced: centers[i] is the band's
// center, and band i covers [edges[i], edges[i + 1]) (edges has one more
// element). An edge is the geometric mean of the centers around it; the outer
// edges are half a step beyond the outer centers.
struct SpectrumBands {
  std::vector<double> centers;
  std::vector<double> edges;

  int count() const { return static_cast<int>(centers.size()); }

  // count (at least 2) bands with centers from lowHz to highHz.
  static SpectrumBands logSpaced(double lowHz, double highHz, int count);
};

// The label under a band: "69" under 1 kHz, "1.2K" under 10 kHz, "14K"
// above.
std::string frequencyLabel(double hz);

#endif // SPECTRUMBANDS_H
