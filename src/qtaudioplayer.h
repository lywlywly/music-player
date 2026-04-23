#ifndef QTAUDIOPLAYER_H
#define QTAUDIOPLAYER_H

#include "audioplayer.h"
#include <QAudioDevice>
#include <QAudioOutput>
#include <QMediaDevices>
#include <QObject>
#include <memory>
#include <qmediaplayer.h>

class QtAudioPlayer : public AudioPlayer {
  Q_OBJECT
public:
  explicit QtAudioPlayer(QObject *parent = nullptr);

  void play() override;
  void pause() override;
  void stop() override;
  void setSource(const QUrl &) override;
  void setPosition(qint64 position) override;

private:
  void createPlaybackObjects();
  void handleAudioOutputsChanged();

  std::unique_ptr<QMediaPlayer> player_;
  QAudioOutput *audioOutput_ = nullptr;
  QMediaDevices mediaDevices_;
};

#endif // QTAUDIOPLAYER_H
