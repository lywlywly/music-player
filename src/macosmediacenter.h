#ifndef MACOSMEDIACENTER_H
#define MACOSMEDIACENTER_H

#include "isystemmediainterface.h"
#include <QObject>
#include <QString>
#include <atomic>

#ifdef __OBJC__
@class MPMediaItemArtwork;
#else
class MPMediaItemArtwork;
#endif

class MacOSMediaCenter : public ISystemMediaInterface {
  Q_OBJECT
public:
  explicit MacOSMediaCenter(QObject *parent = nullptr);
  ~MacOSMediaCenter() override;

  void setTitleAndArtist(const QString &title, const QString &artist) override;
  void setPlaybackState(PlaybackState state) override;
  void setDuration(qint64 durationMs) override;
  void setPosition(qint64 positionMs) override;
  void setArtwork(const QByteArray &imageData) override;

private:
  void connectRemoteCommands();
  void disconnectRemoteCommands();
  void pushNowPlayingToSystem();
  void clearCachedArtwork();
  std::atomic<uint64_t> artworkTaskId_{0};
  MPMediaItemArtwork *cachedArtwork_ = nullptr;
#ifdef __OBJC__
  id playCommandTarget_ = nullptr;
  id pauseCommandTarget_ = nullptr;
  id toggleCommandTarget_ = nullptr;
  id nextCommandTarget_ = nullptr;
  id previousCommandTarget_ = nullptr;
  id seekCommandTarget_ = nullptr;
#else
  void *playCommandTarget_ = nullptr;
  void *pauseCommandTarget_ = nullptr;
  void *toggleCommandTarget_ = nullptr;
  void *nextCommandTarget_ = nullptr;
  void *previousCommandTarget_ = nullptr;
  void *seekCommandTarget_ = nullptr;
#endif
};
#endif // MACOSMEDIACENTER_H
