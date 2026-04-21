#ifndef DUMMYAUDIOPLAYER_H
#define DUMMYAUDIOPLAYER_H

#include "audioplayer.h"

class DummyAudioPlayer : public AudioPlayer {
  Q_OBJECT
public:
  explicit DummyAudioPlayer(QObject *parent = nullptr);
  ~DummyAudioPlayer() override = default;

  void play() override;
  void pause() override;
  void stop() override;
  void setSource(const QUrl &source) override;
  void setPosition(qint64 position) override;

private:
  QUrl source_;
  qint64 durationMs_ = 1000;
  qint64 positionMs_ = 0;
};

#endif // DUMMYAUDIOPLAYER_H
