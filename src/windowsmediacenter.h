#ifndef WINDOWSMEDIACENTER_H
#define WINDOWSMEDIACENTER_H

#include "isystemmediainterface.h"
#include <QObject>
#include <QString>
#include <winrt/Windows.Media.h>
#include <winrt/base.h>

struct WindowsNowPlayingState {
  QString title;
  QString artist;
  qint64 durationMs = 0;
  qint64 positionMs = 0;
  bool isPlaying = false;
};

class WindowsMediaCenter : public ISystemMediaInterface {
  Q_OBJECT
public:
  explicit WindowsMediaCenter(quintptr windowId, QObject *parent = nullptr);
  ~WindowsMediaCenter() override;

  void updateNowPlaying(const QString &title, const QString &artist,
                        qint64 durationMs, qint64 positionMs,
                        bool isPlaying) override;
  void setTrackInfo(const QString &title, const QString &artist,
                    qint64 durationMs) override;
  void updatePosition(qint64 positionMs) override;
  void updatePlaybackState(bool isPlaying) override;

private:
  void initialize();
  void pushNowPlayingToSystem();
  void emitButtonSignal(
      winrt::Windows::Media::SystemMediaTransportControlsButton button);
  quintptr windowId_ = 0;
  WindowsNowPlayingState state_;
  bool initialized_ = false;
  winrt::Windows::Media::SystemMediaTransportControls smtc_{nullptr};
  winrt::event_token buttonPressedToken_{};
};

#endif // WINDOWSMEDIACENTER_H
