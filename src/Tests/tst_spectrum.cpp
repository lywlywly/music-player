#include <QObject>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

#include "../spectrum/fft.h"
#include "../spectrum/pcmtap.h"
#include "../spectrum/spectrumanalyzer.h"
#include "../spectrum/spectrumbands.h"
#include "../spectrum/spectrumsmoother.h"

namespace {
constexpr int kRate = 44100;
constexpr float kFloor = SpectrumAnalyzer::kFloorDb;

std::vector<float> sine(double hz, double amplitude, int count,
                        int rate = kRate) {
  std::vector<float> samples(count);
  for (int i = 0; i < count; ++i) {
    samples[i] = static_cast<float>(
        amplitude * std::sin(2.0 * std::numbers::pi * hz * i / rate));
  }
  return samples;
}

SpectrumBands defaultBands() {
  return SpectrumBands::logSpaced(50.0, 20000.0, 20);
}

int loudestBand(const std::vector<float> &levels) {
  return static_cast<int>(std::max_element(levels.begin(), levels.end()) -
                          levels.begin());
}
} // namespace

class TestSpectrum : public QObject {
  Q_OBJECT

private slots:
  void fft_matchesDft();
  void bands_logSpacedWithLabels();

  void analyzer_fullScaleSineReadsZeroDbInItsBand();
  void analyzer_halfAmplitudeReadsMinusSixDb();
  void analyzer_silenceReadsFloor();
  void analyzer_bandNarrowerThanBinUsesNearestBin();
  void analyzer_bandsAboveNyquistReadFloor();

  void smoother_risesAtOnceFallsAndHoldsPeaks();

  void tap_mixesAndConvertsFormats();
  void tap_readsAtTimeWithZeroFill();
  void tap_startsNewRunOnGapOrRateAndDropsOldAudio();
};

void TestSpectrum::fft_matchesDft() {
  const int n = 16;
  std::vector<std::complex<float>> data(n);
  for (int i = 0; i < n; ++i) {
    data[i] = {static_cast<float>(std::sin(i * 0.7) + 0.3 * i), 0.0f};
  }
  std::vector<std::complex<double>> expected(n);
  for (int k = 0; k < n; ++k) {
    for (int i = 0; i < n; ++i) {
      expected[k] += std::complex<double>(data[i].real(), 0.0) *
                     std::polar(1.0, -2.0 * std::numbers::pi * k * i / n);
    }
  }
  fft(data);
  for (int k = 0; k < n; ++k) {
    QVERIFY2(std::abs(std::complex<double>(data[k]) - expected[k]) < 1e-3,
             qPrintable(QString::number(k)));
  }
}

void TestSpectrum::bands_logSpacedWithLabels() {
  const SpectrumBands bands = defaultBands();
  QCOMPARE(bands.count(), 20);
  QCOMPARE(bands.edges.size(), size_t{21});
  QVERIFY(std::abs(bands.centers.front() - 50.0) < 1e-9);
  QVERIFY(std::abs(bands.centers.back() - 20000.0) < 1e-6);
  for (int i = 0; i < bands.count(); ++i) {
    // Each center is the geometric middle of its edges.
    QVERIFY(std::abs(bands.centers[i] -
                     std::sqrt(bands.edges[i] * bands.edges[i + 1])) < 1e-6);
  }

  QStringList labels;
  for (double center : bands.centers) {
    labels << QString::fromStdString(frequencyLabel(center));
  }
  QCOMPARE(labels,
           QStringList({"50",   "69",   "94",   "129",  "177",  "242",  "332",
                        "455",  "623",  "854",  "1.2K", "1.6K", "2.2K", "3.0K",
                        "4.1K", "5.7K", "7.8K", "11K",  "15K",  "20K"}));
  QCOMPARE(QString::fromStdString(frequencyLabel(9990.0)), QString("10K"));
}

void TestSpectrum::analyzer_fullScaleSineReadsZeroDbInItsBand() {
  SpectrumAnalyzer analyzer(defaultBands());
  const int band = 10; // 1.2K
  // Between two bins: the band still holds the sine's whole power.
  const std::vector<float> levels =
      analyzer.analyze(sine(analyzer.bands().centers[band], 1.0, 4096), kRate);
  QCOMPARE(loudestBand(levels), band);
  QVERIFY2(std::abs(levels[band]) < 0.5,
           qPrintable(QString::number(levels[band])));
  QVERIFY(levels[band - 1] < -40.0f);
  QVERIFY(levels[band + 1] < -40.0f);
}

void TestSpectrum::analyzer_halfAmplitudeReadsMinusSixDb() {
  SpectrumAnalyzer analyzer(defaultBands());
  const std::vector<float> levels =
      analyzer.analyze(sine(analyzer.bands().centers[12], 0.5, 4096), kRate);
  QVERIFY2(std::abs(levels[12] - (-6.02f)) < 0.5,
           qPrintable(QString::number(levels[12])));
}

void TestSpectrum::analyzer_silenceReadsFloor() {
  SpectrumAnalyzer analyzer(defaultBands());
  const std::vector<float> levels =
      analyzer.analyze(std::vector<float>(4096, 0.0f), kRate);
  QVERIFY(std::all_of(levels.begin(), levels.end(),
                      [](float db) { return db == kFloor; }));
}

void TestSpectrum::analyzer_bandNarrowerThanBinUsesNearestBin() {
  SpectrumAnalyzer analyzer(defaultBands());
  // At 192 kHz a bin is 46.9 Hz wide, wider than the 69 band (58-80 Hz),
  // which holds no bin; it takes the nearest one.
  const int rate = 192000;
  const std::vector<float> levels = analyzer.analyze(
      sine(analyzer.bands().centers[1], 1.0, 4096, rate), rate);
  QVERIFY2(levels[1] > -10.0f, qPrintable(QString::number(levels[1])));
}

void TestSpectrum::analyzer_bandsAboveNyquistReadFloor() {
  SpectrumAnalyzer analyzer(defaultBands());
  // White-ish noise at 8 kHz: everything above 4 kHz is above Nyquist.
  std::vector<float> noise(4096);
  uint32_t state = 1;
  for (float &sample : noise) {
    state = state * 1664525u + 1013904223u;
    sample = static_cast<float>(state >> 8) / (1u << 24) - 0.5f;
  }
  const std::vector<float> levels = analyzer.analyze(noise, 8000);
  for (int b = 0; b < analyzer.bands().count(); ++b) {
    if (analyzer.bands().centers[b] >= 4000.0) {
      QCOMPARE(levels[b], kFloor);
    } else {
      QVERIFY(levels[b] > kFloor);
    }
  }
}

void TestSpectrum::smoother_risesAtOnceFallsAndHoldsPeaks() {
  SpectrumSmoother smoother(1, kFloor);
  QVERIFY(smoother.isAtFloor());

  smoother.update({-10.0f}, 0.0);
  QCOMPARE(smoother.levels()[0], -10.0f);
  QCOMPARE(smoother.peaks()[0], -10.0f);
  QVERIFY(!smoother.isAtFloor());

  // Falls at 40 dB/s; the peak holds for 1 s.
  smoother.update({kFloor}, 0.25);
  QCOMPARE(smoother.levels()[0], -20.0f);
  QCOMPARE(smoother.peaks()[0], -10.0f);
  smoother.update({kFloor}, 1.0);
  QCOMPARE(smoother.levels()[0], -50.0f);
  QCOMPARE(smoother.peaks()[0], -10.0f);
  // Then the peak falls at 20 dB/s, and the level reaches the floor.
  smoother.update({kFloor}, 1.5);
  QCOMPARE(smoother.levels()[0], kFloor);
  QCOMPARE(smoother.peaks()[0], -20.0f);
  smoother.update({kFloor}, 4.0);
  QCOMPARE(smoother.peaks()[0], kFloor);
  QVERIFY(smoother.isAtFloor());

  // A peak never stays below its level, and a rise restarts the hold.
  smoother.update({-30.0f}, 4.1);
  QCOMPARE(smoother.peaks()[0], -30.0f);
  smoother.reset();
  QVERIFY(smoother.isAtFloor());
}

void TestSpectrum::tap_mixesAndConvertsFormats() {
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

void TestSpectrum::tap_readsAtTimeWithZeroFill() {
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

void TestSpectrum::tap_startsNewRunOnGapOrRateAndDropsOldAudio() {
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

QTEST_GUILESS_MAIN(TestSpectrum)
#include "tst_spectrum.moc"
