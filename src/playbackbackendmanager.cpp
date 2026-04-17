#include "playbackbackendmanager.h"
#include "gstaudioplayer.h"
#include "qtaudioplayer.h"
#include <QCoreApplication>
#include <QDebug>

PlaybackBackendManager::PlaybackBackendManager(Backend backend, QObject *parent)
    : QObject{parent} {
  qDebug() << "PlaybackBackendManager::PlaybackBackendManager: initialize with "
              "backend = "
           << getBackendName(backend);
  createBackend(backend);
}

PlaybackBackendManager::~PlaybackBackendManager() {
  if (player_) {
    player_->deleteLater();
    player_ = nullptr;
  }

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
  stopGlibLoop();
#endif
}

void PlaybackBackendManager::setBackend(Backend backend) {
  qDebug() << "PlaybackBackendManager::setBackend: backend = "
           << getBackendName(backend);
  if (backend == backend_)
    return;

  stopAndDestroyBackend();

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
  if (backend_ == Backend::GStreamer) {
    stopGlibLoop();
  }
#endif

  createBackend(backend);
  backend_ = backend;
}

void PlaybackBackendManager::createBackend(Backend backend) {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
  if (backend == Backend::GStreamer) {
    startGlibLoop();
  }
#endif

  switch (backend) {
  case Backend::QMediaPlayer:
    player_ = new QtAudioPlayer(this);
    break;
  case Backend::GStreamer:
    player_ = new GstAudioPlayer(this);
    break;
  }
  backend_ = backend;
}

void PlaybackBackendManager::stopAndDestroyBackend() {
  if (!player_)
    return;
  player_->stop();
  player_->deleteLater();
  player_ = nullptr;
}

QString PlaybackBackendManager::getBackendName(Backend backend) {
  switch (backend) {
  case Backend::QMediaPlayer:
    return "QMediaPlayer";
  case Backend::GStreamer:
    return "GStreamer";
  }
  return "Never";
}

void PlaybackBackendManager::startGlibLoop() {
  glibThread_ = std::thread([this] {
    glibLoop_ = g_main_loop_new(nullptr, FALSE);
    glibRunning_ = true;
    g_main_loop_run(glibLoop_);
    g_main_loop_unref(glibLoop_);
    glibLoop_ = nullptr;
    glibRunning_ = false;
  });
}

void PlaybackBackendManager::stopGlibLoop() {
  if (glibLoop_ && glibRunning_)
    g_main_loop_quit(glibLoop_);
  if (glibThread_.joinable())
    glibThread_.join();
}
