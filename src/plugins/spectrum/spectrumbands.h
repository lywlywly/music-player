#ifndef SPECTRUMBANDS_H
#define SPECTRUMBANDS_H

#include <string>
#include <vector>

// Frequency bands of the spectrum: centers[i] is the band's center, and band i
// covers [edges[i], edges[i + 1]) (edges has one more element).
struct SpectrumBands {
  std::vector<double> centers;
  std::vector<double> edges;

  int count() const { return static_cast<int>(centers.size()); }

  // The fractional-octave bands of IEC 61260-1 (base 10, reference 1 kHz),
  // count / 10 bands per octave over the ten octaves from 22.4 Hz to
  // 22.4 kHz. count is a positive multiple of 10. See docs/plugins/spectrum.md.
  static SpectrumBands fractionalOctave(int count);
};

// The label under a band. A center on an ISO 266 preferred frequency (one
// third of an octave apart) shows its preferred value: "31.5", "1K", "16K".
// Others are rounded: "841" under 1 kHz, "1.2K" under 10 kHz, "13K" above.
std::string frequencyLabel(double hz);

#endif // SPECTRUMBANDS_H
