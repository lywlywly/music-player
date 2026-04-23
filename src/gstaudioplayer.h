#ifndef GSTAUDIOPLAYER_H
#define GSTAUDIOPLAYER_H

#include "audioplayer.h"
#include <QByteArray>
#include <QMediaDevices>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <gst/gst.h>

// GStreamer-backed player with ReplayGain support. Runtime output-device
// switching is platform-specific: Linux can work by updating the sink's device
// property directly, while macOS and Windows must first move the pipeline to
// READY, change the sink device, and then seek back to the remembered
// position before resuming. This class keeps the READY/change/seek flow for
// consistent switching behavior across platforms. macOS also differs in device
// identity: osxaudiosink expects the CoreAudio AudioDeviceID, not
// QMediaDevices::defaultAudioOutput().id().
class GstAudioPlayer : public AudioPlayer {
  Q_OBJECT
public:
  explicit GstAudioPlayer(QObject *parent = nullptr);
  ~GstAudioPlayer() override;

  // AudioPlayer interface
public:
  void play() override;
  void pause() override;
  void stop() override;
  void setSource(const QUrl &) override;
  void setPosition(qint64 position) override;

private:
  void initializePipeline();
  void teardownPipeline();
  // QMediaDevices::audioOutputsChanged can fire for transient hardware
  // power-state changes (for example, USB DAC sleep/wake) even when the
  // effective default output device is unchanged. This noisy behavior is
  // currently observed on Windows. We only perform sink reconfiguration when
  // the default output device id actually changes.
  void handleAudioOutputsChanged();
  void updatePosition();
  void emitDurationIfAvailable();
  void applyTrackedSource();
  static void onBusMessage(GstBus *, GstMessage *msg, gpointer user_data);
  GstElement *playbin_;
  GstElement *audioSink_ = nullptr;
  GstElement *rgvolume_ = nullptr; // for ReplayGain
  QMediaDevices mediaDevices_;
  // Cached QMediaDevices::defaultAudioOutput().id() used to ignore noisy
  // audioOutputsChanged emissions (currently observed on Windows) that do not
  // represent a real device change.
  QByteArray lastKnownDefaultOutputId_;
  QUrl currentSource_;
  qint64 trackedPositionMs_ = 0;
  QMediaPlayer::PlaybackState playbackState_ = QMediaPlayer::StoppedState;
  QTimer *positionTimer_;
};

#endif // GSTAUDIOPLAYER_H
