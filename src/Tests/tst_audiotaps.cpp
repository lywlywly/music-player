#include <QAudioBuffer>
#include <QAudioFormat>
#include <QObject>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <gst/gst.h>
#include <vector>

#include "../audiotaps.h"
#include "../spectrum/pcmtap.h"
#include "../spectrum/spectrumanalyzer.h"

class TestAudioTaps : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void gstBuffers_fillTheTapWithStreamTimes_data();
  void gstBuffers_fillTheTapWithStreamTimes();
  void audioBuffers_fillTheTap();
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

// Every format rgvolume can output (that of its inner volume element), and
// audio the tap cannot take.
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
          "audiotestsrc num-buffers=20 samplesperbuffer=1024 freq=1170.8 "
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
  SpectrumAnalyzer analyzer(SpectrumBands::logSpaced(50.0, 20000.0, 20));
  QCOMPARE(tap.read(endUs, samples), 44100);
  const std::vector<float> levels = analyzer.analyze(samples, 44100);
  QCOMPARE(std::max_element(levels.begin(), levels.end()) - levels.begin(), 10);
  QVERIFY2(levels[10] > -0.5f, qPrintable(QString::number(levels[10])));
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

QTEST_GUILESS_MAIN(TestAudioTaps)
#include "tst_audiotaps.moc"
