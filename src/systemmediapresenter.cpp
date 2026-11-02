#include "systemmediapresenter.h"

#include "isystemmediainterface.h"
#include "playbackcontroller.h"

namespace {
ISystemMediaInterface::PlaybackState
systemPlaybackState(PlaybackQueue::PlaybackStatus status) {
  switch (status) {
  case PlaybackQueue::PlaybackStatus::Playing:
    return ISystemMediaInterface::PlaybackState::Playing;
  case PlaybackQueue::PlaybackStatus::Paused:
    return ISystemMediaInterface::PlaybackState::Paused;
  case PlaybackQueue::PlaybackStatus::None:
    break;
  }
  return ISystemMediaInterface::PlaybackState::Stopped;
}
} // namespace

SystemMediaPresenter::SystemMediaPresenter(ISystemMediaInterface &media,
                                           PlaybackController &playback,
                                           QObject *parent)
    : QObject(parent), media_(media) {
  connect(&media, &ISystemMediaInterface::playRequested, &playback,
          &PlaybackController::play);
  connect(&media, &ISystemMediaInterface::pauseRequested, &playback,
          &PlaybackController::pause);
  connect(&media, &ISystemMediaInterface::toggleRequested, &playback,
          &PlaybackController::toggle);
  connect(&media, &ISystemMediaInterface::nextRequested, &playback,
          &PlaybackController::next);
  connect(&media, &ISystemMediaInterface::previousRequested, &playback,
          &PlaybackController::prev);
  connect(&media, &ISystemMediaInterface::seekRequested, &playback,
          &PlaybackController::seek);

  connect(&playback, &PlaybackController::playbackStateChanged, &media,
          [&media](PlaybackQueue::PlaybackStatus status) {
            media.setPlaybackState(systemPlaybackState(status));
          });
  connect(&playback, &PlaybackController::seeking, &media,
          &ISystemMediaInterface::setPosition);
  connect(&playback, &PlaybackController::durationChanged, &media,
          &ISystemMediaInterface::setDuration);
  connect(&playback, &PlaybackController::positionChanged, &media,
          &ISystemMediaInterface::updateCurrentPosition);
}

void SystemMediaPresenter::showSong(const MSong &song) {
  media_.setTitleAndArtist(QString::fromStdString(song.at("title").text),
                           QString::fromStdString(song.at("artist").text));
}

void SystemMediaPresenter::setArtwork(const QByteArray &imageData) {
  media_.setArtwork(imageData);
}
