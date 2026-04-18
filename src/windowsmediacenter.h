#ifndef WINDOWSMEDIACENTER_H
#define WINDOWSMEDIACENTER_H

#include "isystemmediainterface.h"
#include <QObject>
#include <QString>
#include <winrt/Windows.Media.h>
#include <winrt/base.h>
class WindowsMediaCenter : public ISystemMediaInterface {
  Q_OBJECT
public:
  explicit WindowsMediaCenter(quintptr windowId, QObject *parent = nullptr);
  ~WindowsMediaCenter() override;

  void setTitleAndArtist(const QString &title, const QString &artist) override;
  void setPlaybackState(PlaybackState state) override;
  void setDuration(qint64 durationMs) override;
  void setPosition(qint64 positionMs) override;

private:
  void initialize();
  void pushMetadataToSystem();
  void pushPlaybackStateToSystem();
  void pushTimelineToSystem();
  void emitButtonSignal(
      winrt::Windows::Media::SystemMediaTransportControlsButton button);
  quintptr windowId_ = 0;
  bool initialized_ = false;
  winrt::Windows::Media::SystemMediaTransportControls smtc_{nullptr};
  winrt::event_token buttonPressedToken_{};
  winrt::event_token positionChangeRequestedToken_{};
};

#endif // WINDOWSMEDIACENTER_H
