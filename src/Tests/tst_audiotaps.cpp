#include <QAudioBuffer>
#include <QAudioFormat>
#include <QObject>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <gst/gst.h>
#include <vector>

#include "../audiotaps.h"
#include "../pcmtap.h"
#include "testaudiofiles.h"

class TestAudioTaps : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void gstBuffers_fillTheTapWithStreamTimes_data();
  void gstBuffers_fillTheTapWithStreamTimes();
  void audioBuffers_fillTheTap();

  void tap_mixesAndConvertsFormats();
  void tap_readsAtTimeWithZeroFill();
  void tap_startsNewRunOnGapOrRateAndDropsOldAudio();
};

void TestAudioTaps::initTestCase() { gst_init(nullptr, nullptr); }

namespace {
GstPadProbeReturn pushProbe(GstPad *pad, GstPadProbeInfo *info,
                            gpointer userData) {
  pushGstBuffer(pad, gst_pad_probe_info_get_buffer(info),
                *static_cast<PcmTap *>(userData));
  return GST_PAD_PROBE_OK;
}
} // namespace

// Every format the volume element (the audio filter) can output, and audio
// the tap cannot take.
void TestAudioTaps::gstBuffers_fillTheTapWithStreamTimes_data() {
  QTest::addColumn<QString>("format");
  QTest::addColumn<QString>("layout");
  QTest::addColumn<bool>("taken");
  for (const char *format :
       {"F32LE", "F64LE", "S8", "S16LE", "S24LE", "S32LE"}) {
    QTest::newRow(format) << QString(format) << QString("interleaved") << true;
  }
  QTest::newRow("big-endian")
      << QString("S16BE") << QString("interleaved") << false;
  QTest::newRow("non-interleaved")
      << QString("F32LE") << QString("non-interleaved") << false;
}

void TestAudioTaps::gstBuffers_fillTheTapWithStreamTimes() {
  QFETCH(QString, format);
  QFETCH(QString, layout);
  QFETCH(bool, taken);
  // 20 buffers of 1024 stereo frames (0.46 s) of a full-scale 1.2K tone.
  GError *error = nullptr;
  const QByteArray description =
      QStringLiteral(
          "audiotestsrc num-buffers=20 samplesperbuffer=1024 freq=1188.5 "
          "volume=1.0 ! audio/x-raw,format=%1,rate=44100,channels=2,"
          "layout=%2 ! fakesink name=sink sync=false")
          .arg(format, layout)
          .toUtf8();
  GstElement *pipeline = gst_parse_launch(description.constData(), &error);
  QVERIFY2(pipeline != nullptr, error ? error->message : "");
  PcmTap tap;
  GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
  GstPad *pad = gst_element_get_static_pad(sink, "sink");
  gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, &pushProbe, &tap, nullptr);
  gst_object_unref(pad);
  gst_object_unref(sink);

  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  GstBus *bus = gst_element_get_bus(pipeline);
  GstMessage *message = gst_bus_timed_pop_filtered(
      bus, 5 * GST_SECOND,
      static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
  QVERIFY(message != nullptr);
  QCOMPARE(GST_MESSAGE_TYPE(message), GST_MESSAGE_EOS);
  gst_message_unref(message);
  gst_object_unref(bus);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);

  // The audio covers stream times 0 to 20 * 1024 / 44100 s.
  const qint64 endUs = 20LL * 1024 * 1'000'000 / 44100;
  std::vector<float> samples(4096);
  if (!taken) {
    QCOMPARE(tap.read(endUs, samples), 0);
    return;
  }
  // The full-scale tone: a format read wrongly reads as noise, at another
  // level.
  QCOMPARE(tap.read(endUs, samples), 44100);
  const double level = toneLevelDb(samples);
  QVERIFY2(std::abs(level) < 0.5, qPrintable(QString::number(level)));
  QCOMPARE(tap.read(endUs + 200'000, samples), 0);
}

void TestAudioTaps::audioBuffers_fillTheTap() {
  QAudioFormat format;
  format.setSampleFormat(QAudioFormat::Float);
  format.setSampleRate(48000);
  format.setChannelCount(2);
  const std::vector<float> frames = {0.5f, 0.0f, -0.5f, -1.0f};
  const QByteArray data(reinterpret_cast<const char *>(frames.data()),
                        static_cast<qsizetype>(frames.size() * sizeof(float)));

  PcmTap tap;
  // Two frames from stream time 0.5 s.
  pushAudioBuffer(QAudioBuffer(data, format, 500'000), tap);
  std::vector<float> out(2);
  QCOMPARE(tap.read(500'000 + 2 * 1'000'000 / 48000, out), 48000);
  QCOMPARE(out, (std::vector<float>{0.25f, -0.75f}));

  // No start time: skipped.
  tap.clear();
  pushAudioBuffer(QAudioBuffer(data, format), tap);
  QCOMPARE(tap.read(2 * 1'000'000 / 48000, out), 0);
}

void TestAudioTaps::tap_mixesAndConvertsFormats() {
  PcmTap tap;
  std::vector<float> out(2);

  // Stereo Int16: the mean of the channels.
  const int16_t stereo[] = {16384, 0, -32768, -32768};
  tap.push(stereo, 2, 2, PcmTap::SampleFormat::Int16, 1000, 0);
  QCOMPARE(tap.read(2000, out), 1000);
  QCOMPARE(out, (std::vector<float>{0.25f, -1.0f}));

  tap.clear();
  QCOMPARE(tap.read(2000, out), 0);

  const uint8_t u8[] = {128, 255};
  tap.push(u8, 2, 1, PcmTap::SampleFormat::UInt8, 1000, 0);
  tap.read(2000, out);
  QCOMPARE(out[0], 0.0f);
  QVERIFY(std::abs(out[1] - 127.0f / 128.0f) < 1e-6);

  tap.clear();
  const double f64[] = {0.5, -0.25};
  tap.push(f64, 2, 1, PcmTap::SampleFormat::Float64, 1000, 0);
  tap.read(2000, out);
  QCOMPARE(out, (std::vector<float>{0.5f, -0.25f}));

  tap.clear();
  const int8_t s8[] = {-128, 64};
  tap.push(s8, 2, 1, PcmTap::SampleFormat::Int8, 1000, 0);
  tap.read(2000, out);
  QCOMPARE(out, (std::vector<float>{-1.0f, 0.5f}));

  // Packed little-endian 24-bit: 0x400000 (0.5) and 0xC00000 (-0.5).
  tap.clear();
  const uint8_t s24[] = {0x00, 0x00, 0x40, 0x00, 0x00, 0xC0};
  tap.push(s24, 2, 1, PcmTap::SampleFormat::Int24, 1000, 0);
  tap.read(2000, out);
  QCOMPARE(out, (std::vector<float>{0.5f, -0.5f}));

  tap.clear();
  const int32_t s32[] = {1 << 30};
  tap.push(s32, 1, 1, PcmTap::SampleFormat::Int32, 1000, 0);
  std::vector<float> one(1);
  tap.read(1000, one);
  QCOMPARE(one[0], 0.5f);
}

void TestAudioTaps::tap_readsAtTimeWithZeroFill() {
  PcmTap tap;
  // 10 samples at 1 kHz from 1 s: values 1..10.
  std::vector<float> samples(10);
  for (int i = 0; i < 10; ++i) {
    samples[i] = static_cast<float>(i + 1);
  }
  tap.push(samples.data(), 10, 1, PcmTap::SampleFormat::Float32, 1000,
           1'000'000);

  std::vector<float> out(4);
  // Ends at 1.005 s: samples 2..5.
  QCOMPARE(tap.read(1'005'000, out), 1000);
  QCOMPARE(out, (std::vector<float>{2, 3, 4, 5}));
  // Partly before the audio, and partly after it.
  tap.read(1'002'000, out);
  QCOMPARE(out, (std::vector<float>{0, 0, 1, 2}));
  tap.read(1'012'000, out);
  QCOMPARE(out, (std::vector<float>{9, 10, 0, 0}));
  // Entirely outside.
  QCOMPARE(tap.read(1'000'000, out), 0);
  QCOMPARE(tap.read(1'020'000, out), 0);
  QCOMPARE(out, (std::vector<float>{0, 0, 0, 0}));
}

void TestAudioTaps::tap_startsNewRunOnGapOrRateAndDropsOldAudio() {
  PcmTap tap;
  const std::vector<float> ones(1000, 1.0f);
  const std::vector<float> twos(1000, 2.0f);
  std::vector<float> out(1);

  // Following on (1 s of audio at 1 kHz, then the next second).
  tap.push(ones.data(), 1000, 1, PcmTap::SampleFormat::Float32, 1000, 0);
  tap.push(twos.data(), 1000, 1, PcmTap::SampleFormat::Float32, 1000,
           1'000'000);
  QCOMPARE(tap.read(1000, out), 1000);
  QCOMPARE(out[0], 1.0f);
  QCOMPARE(tap.read(2'000'000, out), 1000);
  QCOMPARE(out[0], 2.0f);

  // A third second: the first is dropped (2 s kept).
  tap.push(twos.data(), 1000, 1, PcmTap::SampleFormat::Float32, 1000,
           2'000'000);
  QCOMPARE(tap.read(1000, out), 0);
  QCOMPARE(tap.read(1'001'000, out), 1000);

  // A jump (a seek): a new run.
  tap.push(ones.data(), 1000, 1, PcmTap::SampleFormat::Float32, 1000,
           10'000'000);
  QCOMPARE(tap.read(2'000'000, out), 0);
  QCOMPARE(tap.read(10'001'000, out), 1000);
  QCOMPARE(out[0], 1.0f);

  // A new rate: a new run too.
  tap.push(twos.data(), 1000, 1, PcmTap::SampleFormat::Float32, 2000,
           11'000'000);
  QCOMPARE(tap.read(10'001'000, out), 0);
  QCOMPARE(tap.read(11'001'000, out), 2000);
}

QTEST_GUILESS_MAIN(TestAudioTaps)
#include "tst_audiotaps.moc"
