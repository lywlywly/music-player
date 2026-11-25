#ifndef SPECTRUMSMOOTHER_H
#define SPECTRUMSMOOTHER_H

#include <vector>

// The levels and peak marks shown, per band, following the analyzed levels
// over time: a level rises to its target at once and falls toward it at
// kFallDbPerSecond. A peak follows a level above it, holds for
// kPeakHoldSeconds, then falls at kPeakFallDbPerSecond, never below its
// level. Time is passed in, so tests control it.
class SpectrumSmoother {
public:
  static constexpr float kFallDbPerSecond = 40.0f;
  static constexpr double kPeakHoldSeconds = 1.0;
  static constexpr float kPeakFallDbPerSecond = 20.0f;

  SpectrumSmoother(int bandCount, float floorDb);

  // One target per band, in [floorDb, 0]; nowSeconds never decreases.
  void update(const std::vector<float> &targetsDb, double nowSeconds);
  // Back to the floor, as with no audio.
  void reset();

  const std::vector<float> &levels() const { return levels_; }
  const std::vector<float> &peaks() const { return peaks_; }
  // Every level and peak is at the floor: nothing left to animate.
  bool isAtFloor() const;

private:
  float floorDb_;
  std::vector<float> levels_;
  std::vector<float> peaks_;
  std::vector<double> peakHeldUntil_;
  double lastSeconds_ = -1.0;
};

#endif // SPECTRUMSMOOTHER_H
