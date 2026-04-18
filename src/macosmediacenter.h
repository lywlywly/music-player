#ifndef MACOSMEDIACENTER_H
#define MACOSMEDIACENTER_H

#include "isystemmediainterface.h"
#include <QObject>
#include <QString>

class MacOSMediaCenter : public ISystemMediaInterface {
  Q_OBJECT
public:
  explicit MacOSMediaCenter(QObject *parent = nullptr);

  void setTitleAndArtist(const QString &title, const QString &artist) override;
  void setPlaybackState(PlaybackState state) override;
  void setDuration(qint64 durationMs) override;
  void setPosition(qint64 positionMs) override;

private:
  void connectRemoteCommands();
  void pushNowPlayingToSystem();
};
#endif // MACOSMEDIACENTER_H
