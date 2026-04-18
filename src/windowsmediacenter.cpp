#include "windowsmediacenter.h"

#include <QDebug>
#include <QMetaObject>
#include <systemmediatransportcontrolsinterop.h>
#include <windows.h>
#include <windows.media.h>
#include <winrt/Windows.Foundation.h>

using namespace winrt;
using namespace winrt::Windows::Media;

WindowsMediaCenter::WindowsMediaCenter(quintptr windowId, QObject *parent)
    : ISystemMediaInterface(parent), windowId_(windowId) {
  initialize();
}

WindowsMediaCenter::~WindowsMediaCenter() {
  if (initialized_ && smtc_) {
    smtc_.ButtonPressed(buttonPressedToken_);
  }
}

void WindowsMediaCenter::updateNowPlaying(const QString &title,
                                          const QString &artist,
                                          qint64 durationMs, qint64 positionMs,
                                          bool isPlaying) {
  state_.title = title;
  state_.artist = artist;
  state_.durationMs = durationMs;
  state_.positionMs = positionMs;
  state_.isPlaying = isPlaying;
  pushNowPlayingToSystem();
}

void WindowsMediaCenter::setTrackInfo(const QString &title,
                                      const QString &artist,
                                      qint64 durationMs) {
  state_.title = title;
  state_.artist = artist;
  state_.durationMs = durationMs;
  state_.positionMs = 0;
  state_.isPlaying = true;
  pushNowPlayingToSystem();
}

void WindowsMediaCenter::updatePosition(qint64 positionMs) {
  state_.positionMs = positionMs;
  pushNowPlayingToSystem();
}

void WindowsMediaCenter::updatePlaybackState(bool isPlaying) {
  state_.isPlaying = isPlaying;
  pushNowPlayingToSystem();
}

void WindowsMediaCenter::initialize() {
  if (windowId_ == 0)
    return;

  try {
    try {
      winrt::init_apartment(apartment_type::single_threaded);
    } catch (const hresult_error &e) {
      if (e.code() != RPC_E_CHANGED_MODE) {
        return;
      }
    }

    auto interop =
        winrt::get_activation_factory<SystemMediaTransportControls,
                                      ISystemMediaTransportControlsInterop>();

    ABI::Windows::Media::ISystemMediaTransportControls *rawControls = nullptr;
    const HRESULT hr = interop->GetForWindow(
        reinterpret_cast<HWND>(windowId_),
        __uuidof(ABI::Windows::Media::ISystemMediaTransportControls),
        reinterpret_cast<void **>(&rawControls));
    if (FAILED(hr) || rawControls == nullptr) {
      return;
    }

    smtc_ = {rawControls, winrt::take_ownership_from_abi};
    smtc_.IsEnabled(true);
    smtc_.IsPlayEnabled(true);
    smtc_.IsPauseEnabled(true);
    smtc_.IsNextEnabled(true);
    smtc_.IsPreviousEnabled(true);
    smtc_.DisplayUpdater().Type(MediaPlaybackType::Music);

    buttonPressedToken_ = smtc_.ButtonPressed(
        [this](const SystemMediaTransportControls &,
               const SystemMediaTransportControlsButtonPressedEventArgs &args) {
          emitButtonSignal(args.Button());
        });

    initialized_ = true;
    pushNowPlayingToSystem();
  } catch (...) {
    initialized_ = false;
  }
}

void WindowsMediaCenter::pushNowPlayingToSystem() {
  if (!initialized_ || !smtc_) {
    return;
  }

  auto updater = smtc_.DisplayUpdater();
  updater.Type(MediaPlaybackType::Music);
  updater.MusicProperties().Title(winrt::hstring(state_.title.toStdWString()));
  updater.MusicProperties().Artist(
      winrt::hstring(state_.artist.toStdWString()));
  updater.Update();
  smtc_.PlaybackStatus(state_.isPlaying ? MediaPlaybackStatus::Playing
                                        : MediaPlaybackStatus::Paused);
}

void WindowsMediaCenter::emitButtonSignal(
    SystemMediaTransportControlsButton button) {
  switch (button) {
  case SystemMediaTransportControlsButton::Play:
    QMetaObject::invokeMethod(
        this, [this]() { emit playRequested(); }, Qt::QueuedConnection);
    break;
  case SystemMediaTransportControlsButton::Pause:
    QMetaObject::invokeMethod(
        this, [this]() { emit pauseRequested(); }, Qt::QueuedConnection);
    break;
  case SystemMediaTransportControlsButton::Next:
    QMetaObject::invokeMethod(
        this, [this]() { emit nextRequested(); }, Qt::QueuedConnection);
    break;
  case SystemMediaTransportControlsButton::Previous:
    QMetaObject::invokeMethod(
        this, [this]() { emit previousRequested(); }, Qt::QueuedConnection);
    break;
  default:
    break;
  }
}
