#include "nowplayingpresenter.h"

#include "playbackcontroller.h"
#include <QMainWindow>
#include <QStatusBar>

NowPlayingPresenter::NowPlayingPresenter(const ColumnRegistry &registry,
                                         PlaybackController &playback,
                                         QMainWindow &window)
    : QObject(&window), playback_(playback), window_(window),
      display_(registry), idleTitle_(window.windowTitle()) {
  connect(&playback, &PlaybackController::songAboutToStart, this,
          [this](const MSong &song) { display_.startSong(song); });
  connect(&playback, &PlaybackController::songStarted, this,
          &NowPlayingPresenter::refresh);
  connect(&playback, &PlaybackController::playbackStateChanged, this,
          [this](PlaybackQueue::PlaybackStatus status) {
            if (status == PlaybackQueue::PlaybackStatus::None) {
              display_.stopSong();
            }
            refresh();
          });
  connect(&playback, &PlaybackController::positionChanged, this,
          [this](qint64 positionMs) {
            display_.setPositionMs(positionMs);
            refresh();
          });
  connect(&playback, &PlaybackController::durationChanged, this,
          [this](qint64 durationMs) {
            display_.setDurationMs(durationMs);
            refresh();
          });
  connect(&playback, &PlaybackController::bitrateChanged, this,
          [this](qint64 bitsPerSecond) {
            if (display_.setStreamBitrateBps(bitsPerSecond)) {
              refresh();
            }
          });
  refresh();
}

void NowPlayingPresenter::reloadExpressions() {
  display_.loadExpressions();
  refresh();
}

void NowPlayingPresenter::setExpression(DisplayExpressionKind kind,
                                        const QString &expression) {
  display_.setExpression(kind, expression);
  refresh();
}

const StatusRuntimeSymbolTable &NowPlayingPresenter::runtimeSymbols() const {
  return display_.runtimeSymbols(playback_.status());
}

void NowPlayingPresenter::refresh() {
  const PlaybackQueue::PlaybackStatus status = playback_.status();
  const MSong *song = playback_.currentSong();
  window_.statusBar()->showMessage(display_.statusBarText(status, song));
  window_.setWindowTitle(display_.windowTitle(status, song, idleTitle_));
  emit refreshed();
}
