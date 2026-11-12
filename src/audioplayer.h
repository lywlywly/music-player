#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <QObject>
#include <memory>
#include <mutex>
#include <qmediaplayer.h>

class PcmTap;

class AudioPlayer : public QObject {
  Q_OBJECT
public:
  explicit AudioPlayer(QObject *parent = nullptr);
  virtual ~AudioPlayer() {};
  virtual void play() = 0;
  virtual void pause() = 0;
  virtual void stop() = 0;
  virtual void setSource(const QUrl &) = 0;
  virtual void setPosition(qint64 position) = 0;
  virtual void setVolume(int volumePercent);
  // The decoded audio, before the volume, goes to tap (for the spectrum);
  // nullptr stops it. Players clear the tap on a new source, a seek and stop.
  virtual void setPcmTap(std::shared_ptr<PcmTap> tap);
  // Any thread: the GStreamer player pushes on its streaming thread.
  std::shared_ptr<PcmTap> pcmTap() const;

protected:
  int volumePercent() const;
  void clearPcmTap();

signals:
  void durationChanged(qint64 duration);
  void positionChanged(qint64 milliseconds);
  void bitrateChanged(qint64 bitsPerSecond);
  void mediaStatusChanged(QMediaPlayer::MediaStatus status);

private:
  int volumePercent_ = 100;
  mutable std::mutex pcmTapMutex_;
  std::shared_ptr<PcmTap> pcmTap_;
};

#endif // AUDIOPLAYER_H
