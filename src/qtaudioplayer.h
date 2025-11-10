#ifndef QTAUDIOPLAYER_H
#define QTAUDIOPLAYER_H

#include "audioplayer.h"
#include <QObject>
#include <qmediaplayer.h>

class QtAudioPlayer : public AudioPlayer {
  Q_OBJECT
public:
  explicit QtAudioPlayer(QObject *parent = nullptr);

  // AudioPlayer interface
public:
  void play() override;
  void pause() override;
  void stop() override;
  void setSource(const QUrl &) override;
  void setPosition(qint64 position) override;

private:
  QMediaPlayer p;
};

#endif // QTAUDIOPLAYER_H
