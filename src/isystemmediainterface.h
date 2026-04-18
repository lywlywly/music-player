#ifndef ISYSTEMMEDIAINTERFACE_H
#define ISYSTEMMEDIAINTERFACE_H

#include <QObject>
#include <QString>

class ISystemMediaInterface : public QObject {
  Q_OBJECT
public:
  enum class PlaybackState { Playing, Paused, Stopped };
  struct MediaState {
    QString title;
    QString artist;
    qint64 durationMs = 0;
    qint64 positionMs = 0;
    PlaybackState playbackState = PlaybackState::Stopped;
  };

  explicit ISystemMediaInterface(QObject *parent = nullptr) : QObject(parent) {}
  virtual ~ISystemMediaInterface() = default;
  // Updates the current track title and artist, sets playback state to Playing,
  // resets position to 0, and publishes the change. Do not call this repeatedly
  // for the same song.
  virtual void setTitleAndArtist(const QString &title,
                                 const QString &artist) = 0;
  // Updates the playback state and publishes the change.
  virtual void setPlaybackState(PlaybackState state) = 0;
  // Updates the current track duration and publishes the change.
  virtual void setDuration(qint64 durationMs) = 0;
  // Applies a discontinuous position jump such as a seek or track reset and
  // publishes it. Clients should render ordinary playback progression based on
  // elapsed time, so this should not be called frequently.
  virtual void setPosition(qint64 positionMs) = 0;
  // Updates the current playback position for clients that actively query it;
  // this is currently used only by Linux MPRIS.
  virtual void updateCurrentPosition(qint64 positionMs) {
    state_.positionMs = positionMs;
  }

protected:
  MediaState state_;

signals:
  void playRequested();
  void pauseRequested();
  void toggleRequested();
  void nextRequested();
  void previousRequested();
  void seekRequested(qint64 positionMs);
};
#endif // ISYSTEMMEDIAINTERFACE_H
