#ifndef GSTAUDIOPLAYER_H
#define GSTAUDIOPLAYER_H

#include "audioplayer.h"
#include <QByteArray>
#include <QMediaDevices>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <gst/gst.h>
#include <mutex>

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
  // Starts expensive GStreamer startup work early. On macOS, the prewarmed
  // default output device is consumed only by initial sink setup.
  static void prewarmStartup();

  // AudioPlayer interface
public:
  void play() override;
  void pause() override;
  void stop() override;
  void setSource(const QUrl &) override;
  void setPosition(qint64 position) override;
  void setVolume(int volumePercent) override;
  // Applies the mode's gain from the playing song's stored tags at once; it
  // is heard after the audio already queued behind the filter (about 1.2 s on
  // Windows, see docs/playback.md, ReplayGain).
  void setReplayGainMode(ReplayGainMode mode) override;

private:
  void initializePipeline();
  void teardownPipeline();
  void clearBitrateState();
  void attachBitrateProbeOnDecoder(GstElement *element);
  void detachBitrateProbe();
  // QMediaDevices::audioOutputsChanged can fire for transient hardware
  // power-state changes (for example, USB DAC sleep/wake) even when the
  // effective default output device is unchanged. This noisy behavior is
  // currently observed on Windows. We only perform sink reconfiguration when
  // the default output device id actually changes.
  void handleAudioOutputsChanged();
  void updatePosition();
  void emitDurationIfAvailable();
  void applyTrackedSource();
  static void onElementSetup(GstElement *, GstElement *element,
                             gpointer user_data);
  static GstPadProbeReturn onBitrateProbe(GstPad *, GstPadProbeInfo *info,
                                          gpointer user_data);
  // On replayGainVolume_'s src pad: after ReplayGain, before the volume.
  // Feeds the PcmTap, if any.
  static GstPadProbeReturn onPcmProbe(GstPad *pad, GstPadProbeInfo *info,
                                      gpointer user_data);
  // On replayGainVolume_'s sink pad, streaming thread: a stream-start (a new
  // song) clears the stored tags, a tag event adds to them; both apply the
  // gain. The events pass unchanged.
  static GstPadProbeReturn onTagProbe(GstPad *pad, GstPadProbeInfo *info,
                                      gpointer user_data);
  // Sets replayGainVolume_ from replayGainTags_ and the mode. Called with
  // replayGainMutex_ held.
  void applyReplayGain();
  static void onBusMessage(GstBus *, GstMessage *msg, gpointer user_data);
  GstElement *playbin_;
  GstElement *audioSink_ = nullptr;
  // playbin's audio filter: a volume element set to the ReplayGain gain.
  GstElement *replayGainVolume_ = nullptr;
  // The playing song's tags, written by onTagProbe on the streaming thread
  // and read by setReplayGainMode on the UI thread.
  std::mutex replayGainMutex_;
  ReplayGainTags replayGainTags_;
  QMediaDevices mediaDevices_;
  // Cached QMediaDevices::defaultAudioOutput().id() used to ignore noisy
  // audioOutputsChanged emissions (currently observed on Windows) that do not
  // represent a real device change.
  QByteArray lastKnownDefaultOutputId_;
  QUrl currentSource_;
  qint64 trackedPositionMs_ = 0;
  QMediaPlayer::PlaybackState playbackState_ = QMediaPlayer::StoppedState;
  QTimer *positionTimer_;
  GstPad *bitrateProbePad_ = nullptr;
  gulong bitrateProbeId_ = 0;
  std::atomic<guint64> bitrateWindowBytes_{0};
  qint64 lastBitrateSamplePositionMs_ = -1;
};

#endif // GSTAUDIOPLAYER_H
