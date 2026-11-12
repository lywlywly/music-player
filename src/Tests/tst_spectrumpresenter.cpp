#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "../spectrum/pcmtap.h"
#include "../spectrum/spectrumanalyzer.h"
#include "../spectrum/spectrumpresenter.h"
#include "../spectrum/spectrumwidget.h"

namespace {
constexpr float kFloor = SpectrumAnalyzer::kFloorDb;
// The center of the 1.2K band.
constexpr double kToneHz = 1170.8;
constexpr int kToneBand = 10;

// seconds of a full-scale tone at 44.1 kHz, from stream time 0.
void pushTone(PcmTap &tap, double seconds) {
  const int rate = 44100;
  std::vector<float> samples(static_cast<size_t>(seconds * rate));
  for (size_t i = 0; i < samples.size(); ++i) {
    samples[i] = static_cast<float>(
        std::sin(2.0 * std::numbers::pi * kToneHz * i / rate));
  }
  tap.push(samples.data(), static_cast<int>(samples.size()), 1,
           PcmTap::SampleFormat::Float32, rate, 0);
}

int loudestBand(const std::vector<float> &levels) {
  return static_cast<int>(std::max_element(levels.begin(), levels.end()) -
                          levels.begin());
}
} // namespace

class TestSpectrumPresenter : public QObject {
  Q_OBJECT

private slots:
  void tap_offeredOnlyWhileShown();
  void playing_showsTheToneThenPauseFreezesAndStopFalls();
  void noAudioWhilePlaying_showsTheMessage();
};

void TestSpectrumPresenter::tap_offeredOnlyWhileShown() {
  SpectrumWidget widget;
  SpectrumPresenter presenter(&widget);
  QSignalSpy spy(&presenter, &SpectrumPresenter::tapChanged);
  QCOMPARE(widget.levels().size(), size_t{20});

  widget.show();
  QVERIFY(QTest::qWaitForWindowExposed(&widget));
  QCOMPARE(spy.size(), 1);
  QCOMPARE(spy.at(0).at(0).value<std::shared_ptr<PcmTap>>(), presenter.tap());

  // Hidden: withdrawn, and the audio is forgotten.
  pushTone(*presenter.tap(), 0.5);
  widget.hide();
  QCOMPARE(spy.size(), 2);
  QCOMPARE(spy.at(1).at(0).value<std::shared_ptr<PcmTap>>(), nullptr);
  std::vector<float> out(16);
  QCOMPARE(presenter.tap()->read(100'000, out), 0);
}

void TestSpectrumPresenter::playing_showsTheToneThenPauseFreezesAndStopFalls() {
  SpectrumWidget widget;
  widget.resize(400, 300);
  SpectrumPresenter presenter(&widget);
  widget.show();
  QVERIFY(QTest::qWaitForWindowExposed(&widget));
  pushTone(*presenter.tap(), 2.0);

  // At position 0 no audio ends there yet; the estimate moves on from the
  // last reported position, without another report.
  presenter.setPosition(0);
  presenter.setState(SpectrumPresenter::State::Playing);
  QTRY_VERIFY(widget.levels()[kToneBand] > -1.0f);
  QCOMPARE(loudestBand(widget.levels()), kToneBand);
  QVERIFY(widget.peaks()[kToneBand] >= widget.levels()[kToneBand]);

  presenter.setState(SpectrumPresenter::State::Paused);
  const std::vector<float> frozen = widget.levels();
  QTest::qWait(150);
  QCOMPARE(widget.levels(), frozen);

  // Stopped: no more audio is read, so the bars fall.
  presenter.setState(SpectrumPresenter::State::Stopped);
  QTRY_VERIFY(widget.levels()[kToneBand] < -20.0f);
  QTRY_VERIFY_WITH_TIMEOUT(widget.levels()[kToneBand] == kFloor &&
                               widget.peaks()[kToneBand] == kFloor,
                           6000);
}

void TestSpectrumPresenter::noAudioWhilePlaying_showsTheMessage() {
  SpectrumWidget widget;
  SpectrumPresenter presenter(&widget);
  widget.show();
  QVERIFY(QTest::qWaitForWindowExposed(&widget));
  presenter.setNoAudioMessage("No audio");

  // A backend that does not feed the tap: the message, after a while.
  presenter.setState(SpectrumPresenter::State::Playing);
  QTest::qWait(300);
  QVERIFY(widget.message().isEmpty());
  QTRY_COMPARE(widget.message(), QString("No audio"));

  // Audio arrives: gone.
  pushTone(*presenter.tap(), 2.0);
  presenter.setPosition(500);
  QTRY_VERIFY(widget.message().isEmpty());

  // Stopping clears it too.
  presenter.tap()->clear();
  QTRY_COMPARE(widget.message(), QString("No audio"));
  presenter.setState(SpectrumPresenter::State::Stopped);
  QVERIFY(widget.message().isEmpty());
}

QTEST_MAIN(TestSpectrumPresenter)
#include "tst_spectrumpresenter.moc"
