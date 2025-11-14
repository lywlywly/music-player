#ifndef PLAYBACKBACKENDMANAGER_H
#define PLAYBACKBACKENDMANAGER_H

#include "audioplayer.h"
#include "glib.h"
#include <QObject>
#include <atomic>
#include <thread>

class PlaybackBackendManager : public QObject {
  Q_OBJECT
public:
  enum class Backend { QMediaPlayer, GStreamer };

  explicit PlaybackBackendManager(Backend backend, QObject *parent = nullptr);
  ~PlaybackBackendManager();

  AudioPlayer *player() const { return player_; }
  Backend currentBackend() const { return backend_; }
  void setBackend(Backend backend);

private:
  void startGlibLoop();
  void stopGlibLoop();
  void createBackend(Backend backend);
  void stopAndDestroyBackend();
  static QString getBackendName(Backend backend);

  Backend backend_ = Backend::QMediaPlayer;
  AudioPlayer *player_ = nullptr;

  std::thread glibThread_;
  GMainLoop *glibLoop_ = nullptr;
  std::atomic<bool> glibRunning_{false};
};

#endif // PLAYBACKBACKENDMANAGER_H
