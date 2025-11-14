#ifndef MACOSMEDIACENTER_H
#define MACOSMEDIACENTER_H

#include "isystemmediainterface.h"
#include <QObject>
#include <QString>

struct NowPlayingState {
  QString title;
  QString artist;
  qint64 durationMs = 0;
  qint64 positionMs = 0;
  bool isPlaying = false;
};

class MacOSMediaCenter : public ISystemMediaInterface {
  Q_OBJECT
public:
  explicit MacOSMediaCenter(QObject *parent = nullptr);

  void updateNowPlaying(const QString &title, const QString &artist,
                        qint64 durationMs, qint64 positionMs,
                        bool isPlaying) override;
  void setTrackInfo(const QString &title, const QString &artist,
                    qint64 durationMs) override;
  void updatePosition(qint64 positionMs) override;
  void updatePlaybackState(bool isPlaying) override;

private:
  void connectRemoteCommands();
  void pushNowPlayingToSystem();
  NowPlayingState state_;
};
#endif // MACOSMEDIACENTER_H
