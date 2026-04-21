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
    state_.title = title;
    state_.artist = artist;
    state_.playbackState = PlaybackState::Playing;
    state_.positionMs = 0;
    state_.artworkData.clear();
  }
  void setPlaybackState(PlaybackState state) override {
    state_.playbackState = state;
  }
  void setDuration(qint64 durationMs) override {
    state_.durationMs = durationMs;
  }
  void setPosition(qint64 positionMs) override {
    state_.positionMs = positionMs;
  }
  void updateCurrentPosition(qint64 positionMs) override {
    state_.positionMs = positionMs;
  }
  void setArtwork(const QByteArray &imageData) override {
    state_.artworkData = imageData;
  }

  void requestPlayForTest() { emit playRequested(); }
  void requestPauseForTest() { emit pauseRequested(); }
  void requestToggleForTest() { emit toggleRequested(); }
  void requestNextForTest() { emit nextRequested(); }
  void requestPreviousForTest() { emit previousRequested(); }
  void requestSeekForTest(qint64 positionMs) { emit seekRequested(positionMs); }
  const MediaState &stateForTest() const { return state_; }
};
#endif // DUMMYSYSTEMMEDIAINTERFACE_H
