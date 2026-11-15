#ifndef ABREPEAT_H
#define ABREPEAT_H

#include "iplayerlistener.h"
#include <optional>

class IPlayerControl;

// The A-B loop: points A and B in the playing song; while both are set, a
// position at or past B seeks to A. A song start (the same song again too)
// and a stop clear them. Marks use the last position received, as the host
// has no position query (docs/plugins/abrepeat.md, "Code").
class ABRepeat : public IPlayerListener {
public:
  explicit ABRepeat(IPlayerControl &control);

  void trackStarted(const TrackInfo &track) override;
  void stopped() override;
  void positionChanged(qint64 positionMs) override;

  bool songLoaded() const;
  std::optional<qint64> a() const;
  std::optional<qint64> b() const;

  // Marks A at the last position and clears B.
  void setA();
  // Marks B at the last position, which must be after A; otherwise does
  // nothing.
  void setB();
  void clear();

private:
  IPlayerControl &control_;
  bool songLoaded_ = false;
  qint64 positionMs_ = 0;
  std::optional<qint64> a_;
  std::optional<qint64> b_;
};

#endif // ABREPEAT_H
