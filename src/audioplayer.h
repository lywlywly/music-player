#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <QObject>
#include <qmediaplayer.h>

class AudioPlayer : public QObject {
  Q_OBJECT
public:
  explicit AudioPlayer(QObject *parent = nullptr);
  virtual ~AudioPlayer() {};
  virtual void play() = 0;
  virtual void pause() = 0;
  virtual void setSource(const QUrl &) = 0;
  virtual void setPosition(qint64 position) = 0;

signals:
  void durationChanged(qint64 duration);
  void positionChanged(qint64 milliseconds);
  void mediaStatusChanged(QMediaPlayer::MediaStatus status);
};

#endif // AUDIOPLAYER_H
