#include "spectrumsmoother.h"

#include <algorithm>

SpectrumSmoother::SpectrumSmoother(int bandCount, float floorDb)
    : floorDb_(floorDb), levels_(bandCount, floorDb),
      peaks_(bandCount, floorDb), peakHeldUntil_(bandCount, 0.0) {}

void SpectrumSmoother::update(const std::vector<float> &targetsDb,
                              double nowSeconds) {
  const float elapsed =
      lastSeconds_ < 0.0 ? 0.0f : static_cast<float>(nowSeconds - lastSeconds_);
  lastSeconds_ = nowSeconds;
  for (size_t i = 0; i < levels_.size(); ++i) {
    levels_[i] =
        std::max(targetsDb[i], levels_[i] - kFallDbPerSecond * elapsed);
    if (levels_[i] >= peaks_[i]) {
      peaks_[i] = levels_[i];
      peakHeldUntil_[i] = nowSeconds + kPeakHoldSeconds;
    } else if (nowSeconds > peakHeldUntil_[i]) {
      peaks_[i] =
          std::max(levels_[i], peaks_[i] - kPeakFallDbPerSecond * elapsed);
    }
  }
}

void SpectrumSmoother::reset() {
  std::fill(levels_.begin(), levels_.end(), floorDb_);
  std::fill(peaks_.begin(), peaks_.end(), floorDb_);
  std::fill(peakHeldUntil_.begin(), peakHeldUntil_.end(), 0.0);
  lastSeconds_ = -1.0;
}

bool SpectrumSmoother::isAtFloor() const {
  const auto atFloor = [this](float db) { return db <= floorDb_; };
  return std::all_of(levels_.begin(), levels_.end(), atFloor) &&
         std::all_of(peaks_.begin(), peaks_.end(), atFloor);
}
