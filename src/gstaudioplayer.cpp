#include "gstaudioplayer.h"
#include <iomanip>
#include <qdebug.h>

GstAudioPlayer::GstAudioPlayer(QObject *parent)
    : AudioPlayer{parent}, playbin_(nullptr), rgvolume_(nullptr),
      isPlaying_(false) {
  gst_init(nullptr, nullptr);

  playbin_ = gst_element_factory_make("playbin", "player");
  rgvolume_ = gst_element_factory_make("rgvolume", "rgvolume");

  if (playbin_ && rgvolume_) {
    // Configure ReplayGain behavior
    g_object_set(rgvolume_, "album-mode", FALSE, // Use track gain by default
                 "pre-amp", 0.0, "fallback-gain", 0.0, "headroom", 0.0, NULL);

    // Attach ReplayGain filter to playbin
    g_object_set(playbin_, "audio-filter", rgvolume_, NULL);
  } else {
    qDebug() << "Failed to create GStreamer elements.\n";
  }

  position_timer_ = new QTimer(this);
  connect(position_timer_, &QTimer::timeout, this,
          &GstAudioPlayer::UpdatePosition);
  position_timer_->start(100);

  GstBus *bus = gst_element_get_bus(playbin_);
  gst_bus_add_signal_watch(bus);

  g_signal_connect(bus, "message", G_CALLBACK(&GstAudioPlayer::onBusMessage),
                   this);

  gst_object_unref(bus);
}

GstAudioPlayer::~GstAudioPlayer() {
  if (playbin_) {
    gst_element_set_state(playbin_, GST_STATE_NULL);
    gst_object_unref(playbin_);
  }
  if (rgvolume_) {
    gst_object_unref(rgvolume_);
  }
}

void GstAudioPlayer::play() {
  gst_element_set_state(playbin_, GST_STATE_PLAYING);
  isPlaying_ = true;
}

void GstAudioPlayer::pause() {
  gst_element_set_state(playbin_, GST_STATE_PAUSED);
  isPlaying_ = false;
}

void GstAudioPlayer::stop() {
  gst_element_set_state(playbin_, GST_STATE_NULL);
  isPlaying_ = false;
  emit positionChanged(0);
}

void GstAudioPlayer::setSource(const QUrl &source) {
  gst_element_set_state(playbin_, GST_STATE_READY);
  QString s = source.toString();
  std::string uri = encodeUri(s.toStdString());
  g_object_set(playbin_, "uri", uri.c_str(), NULL);
}

void GstAudioPlayer::setPosition(qint64 milliseconds) {
  gint64 position_ns = static_cast<gint64>(milliseconds) * GST_MSECOND;

  gst_element_seek_simple(
      playbin_, GST_FORMAT_TIME,
      static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
      position_ns);
}

void GstAudioPlayer::emitDurationIfAvailable() {
  gint64 duration_ns = 0;
  if (gst_element_query_duration(playbin_, GST_FORMAT_TIME, &duration_ns)) {
    qint64 duration_ms = static_cast<qint64>(duration_ns / GST_MSECOND);
    emit durationChanged(duration_ms);
  }
}

void GstAudioPlayer::UpdatePosition() {
  gint64 position_ns = 0;
  if (gst_element_query_position(playbin_, GST_FORMAT_TIME, &position_ns) &&
      isPlaying_) {
    emit positionChanged(position_ns / GST_MSECOND);
  }
}

void GstAudioPlayer::onBusMessage(GstBus *bus, GstMessage *msg,
                                  gpointer user_data) {
  Q_UNUSED(bus);
  auto *self = static_cast<GstAudioPlayer *>(user_data);

  switch (GST_MESSAGE_TYPE(msg)) {
  case GST_MESSAGE_EOS:
    emit self->mediaStatusChanged(QMediaPlayer::MediaStatus::EndOfMedia);
    break;
  case GST_MESSAGE_ASYNC_DONE:
    self->emitDurationIfAvailable();
    break;
  default:
    break;
  }
}

std::string GstAudioPlayer::encodeUri(const std::string &path) {
  std::ostringstream encoded;
  encoded << "file://";
  for (unsigned char c : path) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '/') {
      encoded << c;
    } else {
      encoded << '%' << std::setw(2) << std::setfill('0') << std::hex
              << std::uppercase << (int)c;
    }
  }
  return encoded.str();
}
