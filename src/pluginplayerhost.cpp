#include "pluginplayerhost.h"

#include "iplayerlistener.h"
#include "playbackcontroller.h"
#include <algorithm>

namespace {
TrackInfo toTrackInfo(
    const MSong &song,
    const std::unordered_map<std::string, std::string> &remainingFields) {
  TrackInfo track;
  for (const auto &[id, value] : song) {
    track.fields.insert(QString::fromStdString(id),
                        QString::fromStdString(value.text));
  }
  for (const auto &[id, text] : remainingFields) {
    track.tagFields.insert(QString::fromStdString(id),
                           QString::fromStdString(text));
  }
  return track;
}
} // namespace

PluginPlayerHost::PluginPlayerHost(PlaybackController &playback,
                                   QObject *parent)
    : QObject(parent), playback_(playback) {
  connect(
      &playback, &PlaybackController::songStarted, this,
      [this](
          const MSong &song,
          const std::unordered_map<std::string, std::string> &remainingFields) {
        const TrackInfo track = toTrackInfo(song, remainingFields);
        for (IPlayerListener *listener : listeners_) {
          listener->trackStarted(track);
        }
      });
  connect(&playback, &PlaybackController::playbackStateChanged, this,
          [this](PlaybackQueue::PlaybackStatus status) {
            if (status != PlaybackQueue::PlaybackStatus::None) {
              return;
            }
            for (IPlayerListener *listener : listeners_) {
              listener->stopped();
            }
          });
  connect(&playback, &PlaybackController::positionChanged, this,
          [this](qint64 positionMs) {
            for (IPlayerListener *listener : listeners_) {
              listener->positionChanged(positionMs);
            }
          });
}

void PluginPlayerHost::addListener(IPlayerListener *listener) {
  listeners_.push_back(listener);
}

void PluginPlayerHost::removeListener(IPlayerListener *listener) {
  std::erase(listeners_, listener);
}

void PluginPlayerHost::seek(qint64 positionMs) {
  // Queued, so a seek called inside a listener call runs after every listener
  // has the current position, and a seek from another thread runs on the main
  // thread. The host is the context: a seek still queued when it is destroyed
  // is dropped (docs/event-ordering.md, "Plugin commands: the contract").
  QMetaObject::invokeMethod(
      this, [this, positionMs]() { playback_.seek(positionMs); },
      Qt::QueuedConnection);
}
