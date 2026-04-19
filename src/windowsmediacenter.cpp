#include "windowsmediacenter.h"
#include "artworkutils.h"

#include <QMetaObject>
#include <QPointer>
#include <systemmediatransportcontrolsinterop.h>
#include <thread>
#include <windows.h>
#include <winrt/Windows.Foundation.h>

using namespace std::chrono;
using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media;
using namespace winrt::Windows::Storage::Streams;

WindowsMediaCenter::WindowsMediaCenter(quintptr windowId, QObject *parent)
    : ISystemMediaInterface(parent), windowId_(windowId) {
  initialize();
}

WindowsMediaCenter::~WindowsMediaCenter() {
  if (initialized_ && smtc_) {
    smtc_.ButtonPressed(buttonPressedToken_);
    smtc_.PlaybackPositionChangeRequested(positionChangeRequestedToken_);
  }
}

void WindowsMediaCenter::setTitleAndArtist(const QString &title,
                                           const QString &artist) {
  const uint64_t taskId = ++artworkTaskId_;
  state_.title = title;
  state_.artist = artist;
  state_.playbackState = PlaybackState::Playing;
  state_.positionMs = 0;
  state_.artworkData.clear();

  pushMetadataToSystem();
  pushPlaybackStateToSystem();
  pushTimelineToSystem();
  pushArtworkToSystem({}, taskId);
}

void WindowsMediaCenter::setDuration(qint64 durationMs) {
  state_.durationMs = durationMs;
  pushTimelineToSystem();
}

void WindowsMediaCenter::setPlaybackState(PlaybackState state) {
  state_.playbackState = state;
  pushPlaybackStateToSystem();
  if (state == PlaybackState::Stopped) {
    state_.positionMs = 0;
    pushTimelineToSystem();
  }
}

void WindowsMediaCenter::setPosition(qint64 positionMs) {
  state_.positionMs = positionMs;
  pushTimelineToSystem();
}

void WindowsMediaCenter::setArtwork(const QByteArray &imageData) {
  if (state_.artworkData == imageData) {
    return;
  }
  state_.artworkData = imageData;
  const uint64_t taskId = ++artworkTaskId_;

  if (imageData.isEmpty()) {
    pushArtworkToSystem({}, taskId);
    return;
  }

  QPointer<WindowsMediaCenter> self(this);
  const QByteArray imageDataCopy = imageData;
  std::thread([self, taskId, imageDataCopy]() {
    // Keep artwork decode/encode off the UI thread.
    // Resize oversized covers to reduce SMTC payload/update cost.
    const QByteArray preparedArtworkPng =
        ArtworkUtils::normalizeArtworkToPng(imageDataCopy, 256);

    if (!self) {
      return;
    }
    QMetaObject::invokeMethod(
        self,
        [self, taskId, preparedArtworkPng]() {
          if (!self || taskId != self->artworkTaskId_.load()) {
            return;
          }
          self->pushArtworkToSystem(std::move(preparedArtworkPng), taskId);
        },
        Qt::QueuedConnection);
  }).detach();
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
    positionChangeRequestedToken_ = smtc_.PlaybackPositionChangeRequested(
        [this](const SystemMediaTransportControls &,
               const PlaybackPositionChangeRequestedEventArgs &args) {
          const auto requestedMs =
              duration_cast<milliseconds>(args.RequestedPlaybackPosition())
                  .count();
          const qint64 boundedRequestedMs =
              requestedMs < 0 ? 0 : static_cast<qint64>(requestedMs);
          QMetaObject::invokeMethod(
              this,
              [this, boundedRequestedMs]() {
                emit seekRequested(boundedRequestedMs);
              },
              Qt::QueuedConnection);
        });

    initialized_ = true;
    pushMetadataToSystem();
    pushPlaybackStateToSystem();
    pushTimelineToSystem();
  } catch (...) {
    initialized_ = false;
  }
}

void WindowsMediaCenter::pushMetadataToSystem() {
  if (!initialized_ || !smtc_) {
    return;
  }

  auto updater = smtc_.DisplayUpdater();
  updater.Type(MediaPlaybackType::Music);
  updater.MusicProperties().Title(winrt::hstring(state_.title.toStdWString()));
  updater.MusicProperties().Artist(
      winrt::hstring(state_.artist.toStdWString()));
  updater.Update();
}

void WindowsMediaCenter::pushPlaybackStateToSystem() {
  if (!initialized_ || !smtc_) {
    return;
  }

  const auto playbackStatus = state_.playbackState == PlaybackState::Playing
                                  ? MediaPlaybackStatus::Playing
                              : state_.playbackState == PlaybackState::Paused
                                  ? MediaPlaybackStatus::Paused
                                  : MediaPlaybackStatus::Stopped;
  smtc_.PlaybackStatus(playbackStatus);
}

void WindowsMediaCenter::pushTimelineToSystem() {
  if (!initialized_ || !smtc_) {
    return;
  }

  SystemMediaTransportControlsTimelineProperties timeline;

  timeline.StartTime(duration_cast<TimeSpan>(milliseconds(0)));
  timeline.Position(duration_cast<TimeSpan>(milliseconds(state_.positionMs)));
  timeline.EndTime(duration_cast<TimeSpan>(milliseconds(state_.durationMs)));
  timeline.MinSeekTime(duration_cast<TimeSpan>(milliseconds(0)));
  timeline.MaxSeekTime(
      duration_cast<TimeSpan>(milliseconds(state_.durationMs)));

  smtc_.UpdateTimelineProperties(timeline);
}

void WindowsMediaCenter::pushArtworkToSystem(QByteArray imageData,
                                             uint64_t taskId) {
  if (!initialized_ || !smtc_) {
    return;
  }
  auto updater = smtc_.DisplayUpdater();
  if (imageData.isEmpty()) {
    updater.Thumbnail(nullptr);
    updater.Update();
    return;
  }

  InMemoryRandomAccessStream stream;
  DataWriter writer(stream);
  std::vector<uint8_t> bytes(
      reinterpret_cast<const uint8_t *>(imageData.constData()),
      reinterpret_cast<const uint8_t *>(imageData.constData()) +
          imageData.size());
  writer.WriteBytes(bytes);
  QPointer<WindowsMediaCenter> self(this);
  writer.StoreAsync().Completed(
      [self, taskId, updater, writer,
       stream](IAsyncOperation<uint32_t> const &storeOp,
               winrt::Windows::Foundation::AsyncStatus status) mutable {
        if (!self || taskId != self->artworkTaskId_.load()) {
          return;
        }
        if (status != winrt::Windows::Foundation::AsyncStatus::Completed) {
          return;
        }
        try {
          storeOp.GetResults();
          writer.DetachStream();
          stream.Seek(0);
          updater.Thumbnail(
              RandomAccessStreamReference::CreateFromStream(stream));
          updater.Update();
        } catch (...) {
        }
      });
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
