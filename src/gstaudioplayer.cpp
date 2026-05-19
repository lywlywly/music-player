#include "gstaudioplayer.h"
#include <QAudioDevice>
#include <QtGlobal>
#include <cstring>
#include <future>
#include <mutex>
#include <qdebug.h>

namespace {
std::mutex gstInitMutex;
std::shared_future<void> gstInitFuture;

void runGstInit() { gst_init(nullptr, nullptr); }

void ensureGstInitialized() {
  std::shared_future<void> future;
  {
    std::lock_guard<std::mutex> lock(gstInitMutex);
    future = gstInitFuture;
  }
  if (future.valid()) {
    future.get();
    return;
  }
  runGstInit();
}
} // namespace

#if defined(Q_OS_MACOS)
#include <CoreAudio/CoreAudio.h>
namespace {
const char *platformSinkFactoryName() { return "osxaudiosink"; }

std::mutex defaultOutputDeviceMutex;
std::shared_future<AudioDeviceID> defaultOutputDeviceFuture;

AudioDeviceID queryDefaultOutputDeviceId() {
  AudioDeviceID deviceId = kAudioObjectUnknown;
  UInt32 dataSize = sizeof(deviceId);
  AudioObjectPropertyAddress address = {
      kAudioHardwarePropertyDefaultOutputDevice,
      kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};

  const OSStatus status = AudioObjectGetPropertyData(
      kAudioObjectSystemObject, &address, 0, nullptr, &dataSize, &deviceId);
  if (status != noErr) {
    qWarning() << "GstAudioPlayer: failed to query default output device:"
               << status;
    return kAudioObjectUnknown;
  }
  return deviceId;
}

AudioDeviceID defaultOutputDeviceId() {
  std::shared_future<AudioDeviceID> future;
  {
    std::lock_guard<std::mutex> lock(defaultOutputDeviceMutex);
    future = defaultOutputDeviceFuture;
  }
  if (future.valid()) {
    return future.get();
  }
  return queryDefaultOutputDeviceId();
}

bool applyDefaultOutputDevice(GstElement *audioSink) {
  const AudioDeviceID deviceId = defaultOutputDeviceId();
  if (deviceId == kAudioObjectUnknown) {
    return false;
  }
  g_object_set(audioSink, "device", static_cast<gint>(deviceId), NULL);
  return true;
}
} // namespace
#elif defined(Q_OS_WIN) || defined(Q_OS_LINUX)
namespace {
const char *sinkFactoryName(const GstElement *audioSink) {
  if (!audioSink) {
    return nullptr;
  }
  GstElementFactory *factory =
      gst_element_get_factory(const_cast<GstElement *>(audioSink));
  if (!factory) {
    return nullptr;
  }
  return gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
}

#if defined(Q_OS_WIN)
const char *platformSinkFactoryName() { return "wasapi2sink"; }
const char *fallbackSinkFactoryName() { return "directsoundsink"; }

QByteArray directSoundDeviceId(const QByteArray &qtDeviceId) {
  const int guidStart = qtDeviceId.lastIndexOf('{');
  if (guidStart < 0) {
    return qtDeviceId;
  }
  return qtDeviceId.mid(guidStart);
}

bool isDirectSoundSink(const GstElement *audioSink) {
  const gchar *factoryName = sinkFactoryName(audioSink);
  return factoryName != nullptr &&
         qstrcmp(factoryName, fallbackSinkFactoryName()) == 0;
}

QByteArray defaultOutputDeviceId(const GstElement *audioSink) {
  const QByteArray qtDeviceId = QMediaDevices::defaultAudioOutput().id();
  if (qtDeviceId.isEmpty()) {
    return {};
  }
  // wasapi2sink expects the full Windows device id, while directsoundsink
  // expects a GUID-ish form.
  if (isDirectSoundSink(audioSink)) {
    return directSoundDeviceId(qtDeviceId);
  }
  return qtDeviceId;
}
#else
const char *platformSinkFactoryName() { return "pulsesink"; }

QByteArray defaultOutputDeviceId(const GstElement *) {
  return QMediaDevices::defaultAudioOutput().id();
}
#endif

bool applyDefaultOutputDevice(GstElement *audioSink) {
  const QByteArray deviceId = defaultOutputDeviceId(audioSink);
  if (deviceId.isEmpty()) {
    return false;
  }
  g_object_set(audioSink, "device", deviceId.constData(), NULL);
  return true;
}
} // namespace
#endif

void GstAudioPlayer::prewarmStartup() {
  {
    std::lock_guard<std::mutex> lock(gstInitMutex);
    if (!gstInitFuture.valid()) {
      gstInitFuture =
          std::async(std::launch::async, []() { runGstInit(); }).share();
    }
  }
#if defined(Q_OS_MACOS)
  {
    std::lock_guard<std::mutex> lock(defaultOutputDeviceMutex);
    if (!defaultOutputDeviceFuture.valid()) {
      defaultOutputDeviceFuture = std::async(std::launch::async, []() {
                                    return queryDefaultOutputDeviceId();
                                  }).share();
    }
  }
#endif
}

GstAudioPlayer::GstAudioPlayer(QObject *parent)
    : AudioPlayer{parent}, playbin_(nullptr) {
  ensureGstInitialized();

  initializePipeline();
  lastKnownDefaultOutputId_ = QMediaDevices::defaultAudioOutput().id();

  connect(&mediaDevices_, &QMediaDevices::audioOutputsChanged, this,
          [this]() { handleAudioOutputsChanged(); });

  positionTimer_ = new QTimer(this);
  connect(positionTimer_, &QTimer::timeout, this,
          &GstAudioPlayer::updatePosition);
  positionTimer_->start(100);
}

GstAudioPlayer::~GstAudioPlayer() { teardownPipeline(); }

void GstAudioPlayer::play() {
  gst_element_set_state(playbin_, GST_STATE_PLAYING);
  playbackState_ = QMediaPlayer::PlayingState;
}

void GstAudioPlayer::pause() {
  gst_element_set_state(playbin_, GST_STATE_PAUSED);
  playbackState_ = QMediaPlayer::PausedState;
}

void GstAudioPlayer::stop() {
  currentSource_ = QUrl{};
  trackedPositionMs_ = 0;
  playbackState_ = QMediaPlayer::StoppedState;
  clearBitrateState();
  gst_element_set_state(playbin_, GST_STATE_NULL);
  emit positionChanged(0);
  g_object_set(playbin_, "uri", NULL, NULL);
}

void GstAudioPlayer::setSource(const QUrl &source) {
  currentSource_ = source;
  trackedPositionMs_ = 0;
  detachBitrateProbe();
  clearBitrateState();
  gst_element_set_state(playbin_, GST_STATE_READY);
  applyTrackedSource();
}

void GstAudioPlayer::setPosition(qint64 milliseconds) {
  trackedPositionMs_ = milliseconds;
  clearBitrateState();
  gint64 position_ns = static_cast<gint64>(milliseconds) * GST_MSECOND;

  gst_element_seek_simple(
      playbin_, GST_FORMAT_TIME,
      static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
      position_ns);
}

void GstAudioPlayer::emitDurationIfAvailable() {
  if (!playbin_) {
    return;
  }
  gint64 duration_ns = 0;
  if (gst_element_query_duration(playbin_, GST_FORMAT_TIME, &duration_ns)) {
    qint64 duration_ms = static_cast<qint64>(duration_ns / GST_MSECOND);
    emit durationChanged(duration_ms);
  }
}

void GstAudioPlayer::updatePosition() {
  if (!playbin_) {
    return;
  }
  gint64 position_ns = 0;
  if (!gst_element_query_position(playbin_, GST_FORMAT_TIME, &position_ns)) {
    return;
  }

  trackedPositionMs_ = position_ns / GST_MSECOND;
  if (playbackState_ != QMediaPlayer::PlayingState) {
    return;
  }

  if (lastBitrateSamplePositionMs_ < 0) {
    bitrateWindowBytes_.store(0);
    lastBitrateSamplePositionMs_ = trackedPositionMs_;
  } else {
    const qint64 deltaMs = trackedPositionMs_ - lastBitrateSamplePositionMs_;
    if (deltaMs >= 1000) {
      const guint64 bytes = bitrateWindowBytes_.exchange(0);
      const qint64 bitrateBps =
          bytes > 0 ? static_cast<qint64>(bytes * 8 * 1000 / deltaMs) : 0;
      emit bitrateChanged(bitrateBps);
      lastBitrateSamplePositionMs_ = trackedPositionMs_;
    }
  }
  emit positionChanged(trackedPositionMs_);
}

void GstAudioPlayer::onElementSetup(GstElement *, GstElement *element,
                                    gpointer user_data) {
  auto *self = static_cast<GstAudioPlayer *>(user_data);
  self->attachBitrateProbeOnDecoder(element);
}

GstPadProbeReturn GstAudioPlayer::onBitrateProbe(GstPad *,
                                                 GstPadProbeInfo *info,
                                                 gpointer user_data) {
  auto *self = static_cast<GstAudioPlayer *>(user_data);
  GstBuffer *buffer = gst_pad_probe_info_get_buffer(info);
  if (!buffer) {
    return GST_PAD_PROBE_OK;
  }
  self->bitrateWindowBytes_.fetch_add(
      static_cast<guint64>(gst_buffer_get_size(buffer)));
  return GST_PAD_PROBE_OK;
}

void GstAudioPlayer::onBusMessage(GstBus *, GstMessage *msg,
                                  gpointer user_data) {
  auto *self = static_cast<GstAudioPlayer *>(user_data);

  switch (GST_MESSAGE_TYPE(msg)) {
  case GST_MESSAGE_EOS:
    emit self->mediaStatusChanged(QMediaPlayer::MediaStatus::EndOfMedia);
    break;
  case GST_MESSAGE_ASYNC_DONE:
    self->emitDurationIfAvailable();
    break;
  case GST_MESSAGE_ERROR: {
    GError *error = nullptr;
    gchar *debugInfo = nullptr;
    gst_message_parse_error(msg, &error, &debugInfo);
    qWarning() << "GstAudioPlayer error:"
               << (error ? QString::fromUtf8(error->message) : QString{})
               << (debugInfo ? QString::fromUtf8(debugInfo) : QString{});
    if (error) {
      g_error_free(error);
    }
    if (debugInfo) {
      g_free(debugInfo);
    }
    break;
  }
  default:
    break;
  }
}

void GstAudioPlayer::initializePipeline() {
  playbin_ = gst_element_factory_make("playbin", "player");
  rgvolume_ = gst_element_factory_make("rgvolume", "rgvolume");

  if (!playbin_ || !rgvolume_) {
    qFatal("GstAudioPlayer: failed to create GStreamer elements");
  }
  gst_object_ref(rgvolume_);

  g_object_set(rgvolume_, "album-mode", FALSE, "pre-amp", 0.0, "fallback-gain",
               0.0, "headroom", 0.0, NULL);
  g_object_set(playbin_, "audio-filter", rgvolume_, NULL);

  audioSink_ =
      gst_element_factory_make(platformSinkFactoryName(), "audio_sink");
#if defined(Q_OS_WIN)
  if (!audioSink_) {
    qWarning("GstAudioPlayer: failed to create wasapi2sink, falling back to "
             "directsoundsink");
    audioSink_ =
        gst_element_factory_make(fallbackSinkFactoryName(), "audio_sink");
  }
#endif
  if (!audioSink_) {
    qFatal("GstAudioPlayer: failed to create platform audio sink");
  }
  gst_object_ref(audioSink_);

  g_object_set(playbin_, "audio-sink", audioSink_, NULL);

  applyDefaultOutputDevice(audioSink_);

  GstBus *bus = gst_element_get_bus(playbin_);
  if (!bus) {
    qFatal("GstAudioPlayer: failed to get pipeline bus");
  }
  gst_bus_add_signal_watch(bus);
  g_signal_connect(bus, "message", G_CALLBACK(&GstAudioPlayer::onBusMessage),
                   this);
  gst_object_unref(bus);
  g_signal_connect(playbin_, "element-setup",
                   G_CALLBACK(&GstAudioPlayer::onElementSetup), this);
}

void GstAudioPlayer::teardownPipeline() {
  detachBitrateProbe();
  if (playbin_) {
    GstBus *bus = gst_element_get_bus(playbin_);
    if (bus) {
      g_signal_handlers_disconnect_by_data(bus, this);
      gst_bus_remove_signal_watch(bus);
      gst_object_unref(bus);
    }
    gst_element_set_state(playbin_, GST_STATE_NULL);
  }
  if (playbin_) {
    gst_object_unref(playbin_);
    playbin_ = nullptr;
  }
  if (audioSink_) {
    gst_object_unref(audioSink_);
    audioSink_ = nullptr;
  }
  if (rgvolume_) {
    gst_object_unref(rgvolume_);
    rgvolume_ = nullptr;
  }
}

void GstAudioPlayer::clearBitrateState() {
  lastBitrateSamplePositionMs_ = -1;
  bitrateWindowBytes_.store(0);
  emit bitrateChanged(0);
}

void GstAudioPlayer::attachBitrateProbeOnDecoder(GstElement *element) {
  // One decoder probe is enough for bitrate estimation in the current pipeline.
  if (bitrateProbeId_ != 0) {
    return;
  }
  if (!element) {
    return;
  }
  GstElementFactory *factory = gst_element_get_factory(element);
  if (!factory) {
    return;
  }
  const gchar *klass =
      gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_KLASS);
  if (!klass || !std::strstr(klass, "Decoder/Audio")) {
    return;
  }

  GstPad *sinkPad = gst_element_get_static_pad(element, "sink");
  if (!sinkPad) {
    return;
  }

  bitrateProbeId_ =
      gst_pad_add_probe(sinkPad, GST_PAD_PROBE_TYPE_BUFFER,
                        &GstAudioPlayer::onBitrateProbe, this, nullptr);
  bitrateProbePad_ = sinkPad;
  lastBitrateSamplePositionMs_ = -1;
  bitrateWindowBytes_.store(0);
}

void GstAudioPlayer::detachBitrateProbe() {
  if (bitrateProbeId_ != 0) {
    gst_pad_remove_probe(bitrateProbePad_, bitrateProbeId_);
  }
  bitrateProbeId_ = 0;
  if (bitrateProbePad_) {
    gst_object_unref(bitrateProbePad_);
    bitrateProbePad_ = nullptr;
  }
}

void GstAudioPlayer::applyTrackedSource() {
  if (currentSource_.isEmpty()) {
    g_object_set(playbin_, "uri", NULL, NULL);
    return;
  }

  g_object_set(
      playbin_, "uri",
      g_filename_to_uri(currentSource_.toString().toUtf8(), NULL, NULL), NULL);
}

void GstAudioPlayer::handleAudioOutputsChanged() {
  // Ignore noisy output-list notifications unless the effective default device
  // id actually changed
  const QByteArray currentDefaultOutputId =
      QMediaDevices::defaultAudioOutput().id();
  if (currentDefaultOutputId == lastKnownDefaultOutputId_) {
    return;
  }
  lastKnownDefaultOutputId_ = currentDefaultOutputId;

  if (!playbin_) {
    initializePipeline();
    return;
  }

  if (!audioSink_) {
    return;
  }

  GstState currentState = GST_STATE_NULL;
  GstState pendingState = GST_STATE_VOID_PENDING;
  gst_element_get_state(playbin_, &currentState, &pendingState,
                        GST_CLOCK_TIME_NONE);
  const GstState targetState =
      pendingState != GST_STATE_VOID_PENDING ? pendingState : currentState;

  gint64 positionNs = GST_CLOCK_TIME_NONE;
  if (targetState == GST_STATE_PLAYING || targetState == GST_STATE_PAUSED) {
    gst_element_query_position(playbin_, GST_FORMAT_TIME, &positionNs);
  }

  if (gst_element_set_state(playbin_, GST_STATE_READY) ==
      GST_STATE_CHANGE_FAILURE) {
    qWarning("GstAudioPlayer: failed to move pipeline to READY");
    return;
  }
  gst_element_get_state(playbin_, nullptr, nullptr, GST_CLOCK_TIME_NONE);

  applyDefaultOutputDevice(audioSink_);

  if (targetState == GST_STATE_PLAYING || targetState == GST_STATE_PAUSED) {
    if (gst_element_set_state(playbin_, GST_STATE_PAUSED) ==
        GST_STATE_CHANGE_FAILURE) {
      qWarning("GstAudioPlayer: failed to move pipeline to PAUSED");
      return;
    }
    gst_element_get_state(playbin_, nullptr, nullptr, GST_CLOCK_TIME_NONE);

    if (GST_CLOCK_TIME_IS_VALID(positionNs)) {
      gst_element_seek_simple(playbin_, GST_FORMAT_TIME,
                              static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH |
                                                        GST_SEEK_FLAG_KEY_UNIT),
                              positionNs);
      trackedPositionMs_ = positionNs / GST_MSECOND;
    }
  }

  if (targetState == GST_STATE_PLAYING) {
    gst_element_set_state(playbin_, GST_STATE_PLAYING);
    playbackState_ = QMediaPlayer::PlayingState;
  } else if (targetState == GST_STATE_PAUSED) {
    gst_element_set_state(playbin_, GST_STATE_PAUSED);
    playbackState_ = QMediaPlayer::PausedState;
  }
}
