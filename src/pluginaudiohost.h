#ifndef PLUGINAUDIOHOST_H
#define PLUGINAUDIOHOST_H

#include "playpositionestimate.h"
#include <QElapsedTimer>
#include <QObject>
#include <memory>
#include <span>

class PcmTap;
class PlaybackController;

// The decoded audio for plugins, shared by every plugin's IAudioHost
// (docs/plugins/plugin-system.md, "Audio for plugins"): one tap, on the
// player while some plugin wants audio, and the play position estimate that
// reads are made at. Frontend class.
class PluginAudioHost : public QObject {
  Q_OBJECT

public:
  explicit PluginAudioHost(PlaybackController &playback,
                           QObject *parent = nullptr);

  // A plugin starts or stops wanting audio; each plugin counts once. The
  // first gives the player the tap; after the last, it is withdrawn and
  // cleared.
  void addUser();
  void removeUser();
  // The samples ending at the estimated play position (IAudioHost::read).
  int read(std::span<float> out) const;

private:
  // Destroyed before this host (MainWindow creates it first), so it is not
  // called from the destructor; the player's shared_ptr keeps the tap alive.
  PlaybackController &playback_;
  std::shared_ptr<PcmTap> tap_;
  PlayPositionEstimate estimate_;
  QElapsedTimer clock_;
  int users_ = 0;
};

#endif // PLUGINAUDIOHOST_H
