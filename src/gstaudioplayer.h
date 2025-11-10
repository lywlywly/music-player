#ifndef GSTAUDIOPLAYER_H
#define GSTAUDIOPLAYER_H

#include "audioplayer.h"
#include <QObject>
#include <QTimer>
#include <gst/gst.h>

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
  void UpdatePosition();
  void emitDurationIfAvailable();
  static void onBusMessage(GstBus *bus, GstMessage *msg, gpointer user_data);
  static std::string encodeUri(const std::string &path);
  GstElement *playbin_;
  GstElement *rgvolume_; // for ReplayGain
  bool isPlaying_;
  QTimer *position_timer_;
};

#endif // GSTAUDIOPLAYER_H
