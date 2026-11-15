#include "abrepeat.h"

#include "iplayercontrol.h"

ABRepeat::ABRepeat(IPlayerControl &control) : control_(control) {}

void ABRepeat::trackStarted(const TrackInfo & /*track*/) {
  songLoaded_ = true;
  positionMs_ = 0;
  clear();
}

void ABRepeat::stopped() {
  songLoaded_ = false;
  positionMs_ = 0;
  clear();
}

void ABRepeat::positionChanged(qint64 positionMs) {
  positionMs_ = positionMs;
  // The seek is deferred (IPlayerControl), so it runs after this delivery,
  // and its position arrives as a later positionChanged.
  if (a_ && b_ && positionMs >= *b_) {
    control_.seek(*a_);
  }
}

bool ABRepeat::songLoaded() const { return songLoaded_; }

std::optional<qint64> ABRepeat::a() const { return a_; }

std::optional<qint64> ABRepeat::b() const { return b_; }

void ABRepeat::setA() {
  a_ = positionMs_;
  b_.reset();
}

void ABRepeat::setB() {
  if (!a_ || positionMs_ <= *a_) {
    return;
  }
  b_ = positionMs_;
}

void ABRepeat::clear() {
  a_.reset();
  b_.reset();
}
