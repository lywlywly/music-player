#ifndef ISYSTEMMEDIAINTERFACE_H
#define ISYSTEMMEDIAINTERFACE_H

#include <QObject>
#include <QString>

class ISystemMediaInterface : public QObject {
  Q_OBJECT
public:
  explicit ISystemMediaInterface(QObject *parent = nullptr) : QObject(parent) {}
  virtual ~ISystemMediaInterface() = default;
  virtual void updateNowPlaying(const QString &title, const QString &artist,
                                qint64 durationMs, qint64 positionMs,
                                bool isPlaying) = 0;
  virtual void setTrackInfo(const QString &title, const QString &artist,
                            qint64 durationMs) = 0;
  virtual void updatePosition(qint64 positionMs) = 0;
  virtual void updatePlaybackState(bool isPlaying) = 0;
signals:
  void playRequested();
  void pauseRequested();
  void toggleRequested();
  void nextRequested();
  void previousRequested();
};
#endif // ISYSTEMMEDIAINTERFACE_H
