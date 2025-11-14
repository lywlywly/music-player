#ifndef DUMMYSYSTEMMEDIAINTERFACE_H
#define DUMMYSYSTEMMEDIAINTERFACE_H

#include "isystemmediainterface.h"

class DummySystemMediaInterface : public ISystemMediaInterface {
  Q_OBJECT
public:
  explicit DummySystemMediaInterface(QObject *parent = nullptr)
      : ISystemMediaInterface(parent) {}
  ~DummySystemMediaInterface() override = default;
  void updateNowPlaying(const QString &, const QString &, qint64, qint64,
                        bool) override {}
  void setTrackInfo(const QString &title, const QString &artist,
                    qint64 durationMs) override {}
  void updatePosition(qint64 positionMs) override {}
  void updatePlaybackState(bool isPlaying) override {}
};
#endif // DUMMYSYSTEMMEDIAINTERFACE_H
