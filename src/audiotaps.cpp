#include "audiotaps.h"

#include "spectrum/pcmtap.h"
#include <QAudioBuffer>
#include <cstring>

namespace {

struct CapsFormat {
  const char *name;
  PcmTap::SampleFormat format;
  int bytesPerSample;
};

// Core GStreamer only (not gstreamer-audio): the format names of the caps.
// These are the formats of rgvolume's inner volume element, which ends the
// audio-filter: its advertised F32LE/S16LE is only the bin's template (a
// 24-bit FLAC comes out as S24LE). The platforms the app supports are
// little-endian.
constexpr CapsFormat kCapsFormats[] = {
    {"F32LE", PcmTap::SampleFormat::Float32, 4},
    {"F64LE", PcmTap::SampleFormat::Float64, 8},
    {"S8", PcmTap::SampleFormat::Int8, 1},
    {"S16LE", PcmTap::SampleFormat::Int16, 2},
    {"S24LE", PcmTap::SampleFormat::Int24, 3},
    {"S32LE", PcmTap::SampleFormat::Int32, 4},
};

const CapsFormat *capsFormat(const char *name) {
  for (const CapsFormat &format : kCapsFormats) {
    if (std::strcmp(name, format.name) == 0) {
      return &format;
    }
  }
  return nullptr;
}

// The stream time of the buffer's PTS, or -1 when it has none. Buffers on a
// pad come after its caps and segment.
gint64 streamTimeUs(GstPad *pad, GstBuffer *buffer) {
  GstEvent *event = gst_pad_get_sticky_event(pad, GST_EVENT_SEGMENT, 0);
  const GstSegment *segment = nullptr;
  gst_event_parse_segment(event, &segment);
  const GstClockTime streamTime = gst_segment_to_stream_time(
      segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));
  gst_event_unref(event);
  return GST_CLOCK_TIME_IS_VALID(streamTime)
             ? static_cast<gint64>(streamTime / GST_USECOND)
             : -1;
}

} // namespace

void pushGstBuffer(GstPad *pad, GstBuffer *buffer, PcmTap &tap) {
  // Raw audio caps always hold format, layout, rate and channels.
  GstCaps *caps = gst_pad_get_current_caps(pad);
  const GstStructure *structure = gst_caps_get_structure(caps, 0);
  const CapsFormat *format =
      capsFormat(gst_structure_get_string(structure, "format"));
  const bool interleaved =
      std::strcmp(gst_structure_get_string(structure, "layout"),
                  "interleaved") == 0;
  int rate = 0;
  int channels = 0;
  gst_structure_get_int(structure, "rate", &rate);
  gst_structure_get_int(structure, "channels", &channels);
  gst_caps_unref(caps);
  if (format == nullptr || !interleaved) {
    return;
  }
  const gint64 startUs = streamTimeUs(pad, buffer);
  GstMapInfo map;
  if (startUs < 0 || !gst_buffer_map(buffer, &map, GST_MAP_READ)) {
    return;
  }
  const int frames =
      static_cast<int>(map.size / (format->bytesPerSample * channels));
  tap.push(map.data, frames, channels, format->format, rate, startUs);
  gst_buffer_unmap(buffer, &map);
}

void pushAudioBuffer(const QAudioBuffer &buffer, PcmTap &tap) {
  const QAudioFormat format = buffer.format();
  PcmTap::SampleFormat sampleFormat;
  switch (format.sampleFormat()) {
  case QAudioFormat::UInt8:
    sampleFormat = PcmTap::SampleFormat::UInt8;
    break;
  case QAudioFormat::Int16:
    sampleFormat = PcmTap::SampleFormat::Int16;
    break;
  case QAudioFormat::Int32:
    sampleFormat = PcmTap::SampleFormat::Int32;
    break;
  case QAudioFormat::Float:
    sampleFormat = PcmTap::SampleFormat::Float32;
    break;
  default:
    return;
  }
  // -1 when the backend does not know it.
  if (buffer.startTime() < 0) {
    return;
  }
  tap.push(buffer.constData<char>(), static_cast<int>(buffer.frameCount()),
           format.channelCount(), sampleFormat, format.sampleRate(),
           buffer.startTime());
}
