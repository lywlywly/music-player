#ifndef DUMMYSYSTEMMEDIAINTERFACE_H
#define DUMMYSYSTEMMEDIAINTERFACE_H

#include "isystemmediainterface.h"

class DummySystemMediaInterface : public ISystemMediaInterface {
  Q_OBJECT
public:
  explicit DummySystemMediaInterface(QObject *parent = nullptr)
      : ISystemMediaInterface(parent) {}
  ~DummySystemMediaInterface() override = default;
  void setTitleAndArtist(const QString &title, const QString &artist) override {
  }
  void setPlaybackState(PlaybackState state) override {}
  void setDuration(qint64 durationMs) override {}
  void setPosition(qint64 positionMs) override {}
};
#endif // DUMMYSYSTEMMEDIAINTERFACE_H
