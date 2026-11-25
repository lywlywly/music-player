#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QObject>
#include <QSettings>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <span>
#include <vector>

#include "../pluginapi/iaudiohost.h"
#include "../pluginapi/iplayerhost.h"
#include "../plugins/spectrum/spectrumanalyzer.h"
#include "../plugins/spectrum/spectrumpresenter.h"
#include "../plugins/spectrum/spectrumwidget.h"

namespace {
constexpr float kFloor = SpectrumAnalyzer::kFloorDb;
// The center of the 1.2K band.
constexpr double kToneHz = 1188.5;
constexpr int kToneBand = 11;

// Keeps the one listener the presenter adds.
class TestPlayerHost : public IPlayerHost {
public:
  void addListener(IPlayerListener *added) override { listener = added; }
  void removeListener(IPlayerListener *) override { listener = nullptr; }

  IPlayerListener *listener = nullptr;
};

// Audio as the host reads it at the play position: while hasAudio, a
// full-scale tone at 44.1 kHz; otherwise none.
class TestAudioHost : public IAudioHost {
public:
  void setWanted(bool value) override { wanted = value; }
  int read(std::span<float> out) const override {
    if (!hasAudio) {
      std::ranges::fill(out, 0.0f);
      return 0;
    }
    for (size_t i = 0; i < out.size(); ++i) {
      out[i] = static_cast<float>(
          std::sin(2.0 * std::numbers::pi * kToneHz * i / kRate));
    }
    return kRate;
  }

  static constexpr int kRate = 44100;
  bool wanted = false;
  bool hasAudio = false;
};

int loudestBand(const std::vector<float> &levels) {
  return static_cast<int>(std::max_element(levels.begin(), levels.end()) -
                          levels.begin());
}
} // namespace

class TestSpectrumPresenter : public QObject {
  Q_OBJECT

private slots:
  void init() { QSettings().clear(); }
  void audio_wantedOnlyWhileShown();
  void playing_showsTheToneThenPauseFreezesAndStopFalls();
  void noAudioWhilePlaying_showsTheMessage();
  void contextMenu_setsAndSavesTheBandCount();
};

void TestSpectrumPresenter::audio_wantedOnlyWhileShown() {
  SpectrumWidget widget;
  TestPlayerHost player;
  TestAudioHost audio;
  auto presenter = std::make_unique<SpectrumPresenter>(&widget, player, audio);
  QCOMPARE(player.listener, presenter.get());
  QCOMPARE(widget.levels().size(), size_t{20});
  QVERIFY(!audio.wanted);

  widget.show();
  QVERIFY(QTest::qWaitForWindowExposed(&widget));
  QVERIFY(audio.wanted);
  widget.hide();
  QVERIFY(!audio.wanted);

  // Destroyed while shown: it stops wanting audio and listening.
  widget.show();
  QVERIFY(audio.wanted);
  presenter.reset();
  QVERIFY(!audio.wanted);
  QCOMPARE(player.listener, nullptr);
}

void TestSpectrumPresenter::playing_showsTheToneThenPauseFreezesAndStopFalls() {
  SpectrumWidget widget;
  widget.resize(400, 300);
  TestPlayerHost player;
  TestAudioHost audio;
  audio.hasAudio = true;
  SpectrumPresenter presenter(&widget, player, audio);
  widget.show();
  QVERIFY(QTest::qWaitForWindowExposed(&widget));

  presenter.trackStarted({});
  QTRY_VERIFY(widget.levels()[kToneBand] > -1.0f);
  QCOMPARE(loudestBand(widget.levels()), kToneBand);
  QVERIFY(widget.peaks()[kToneBand] >= widget.levels()[kToneBand]);

  presenter.paused();
  const std::vector<float> frozen = widget.levels();
  QTest::qWait(150);
  QCOMPARE(widget.levels(), frozen);

  // Stopped: no more audio is read, so the bars fall.
  presenter.stopped();
  QTRY_VERIFY(widget.levels()[kToneBand] < -20.0f);
  QTRY_VERIFY_WITH_TIMEOUT(widget.levels()[kToneBand] == kFloor &&
                               widget.peaks()[kToneBand] == kFloor,
                           6000);
}

void TestSpectrumPresenter::noAudioWhilePlaying_showsTheMessage() {
  SpectrumWidget widget;
  TestPlayerHost player;
  TestAudioHost audio;
  SpectrumPresenter presenter(&widget, player, audio);
  widget.show();
  QVERIFY(QTest::qWaitForWindowExposed(&widget));

  // A backend that sends no audio: the message, after a while. A resumed
  // song plays as a started one.
  presenter.paused();
  presenter.resumed();
  QTest::qWait(300);
  QVERIFY(widget.message().isEmpty());
  QTRY_VERIFY(widget.message().contains("Playback backend"));

  // Audio arrives: gone.
  audio.hasAudio = true;
  QTRY_VERIFY(widget.message().isEmpty());

  // Stopping clears it too.
  audio.hasAudio = false;
  QTRY_VERIFY(!widget.message().isEmpty());
  presenter.stopped();
  QVERIFY(widget.message().isEmpty());
}

void TestSpectrumPresenter::contextMenu_setsAndSavesTheBandCount() {
  SpectrumWidget widget;
  widget.resize(400, 300);
  TestPlayerHost player;
  TestAudioHost audio;
  audio.hasAudio = true;
  SpectrumPresenter presenter(&widget, player, audio);
  widget.show();
  QVERIFY(QTest::qWaitForWindowExposed(&widget));
  presenter.trackStarted({});
  QTRY_VERIFY(widget.levels()[kToneBand] > -1.0f);

  // The menu lists every count, the current one checked; pick 80.
  QStringList shown;
  QString checked;
  QTimer::singleShot(0, [&] {
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    QVERIFY(menu);
    for (QAction *action : menu->actions()) {
      shown << action->text();
      if (action->isChecked()) {
        checked = action->text();
      }
    }
    menu->actions().last()->trigger();
    menu->close();
  });
  QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(10, 10),
                          widget.mapToGlobal(QPoint(10, 10)));
  QApplication::sendEvent(&widget, &event);
  QCOMPARE(shown,
           (QStringList{"10 Bands", "20 Bands", "40 Bands", "80 Bands"}));
  QCOMPARE(checked, QString("20 Bands"));

  // 80 bars, and the tone loudest in its band. The narrower bands split its
  // power with a neighbor, so it reads below 0 dB.
  QCOMPARE(presenter.bandCount(), 80);
  QCOMPARE(widget.levels().size(), size_t{80});
  QTRY_VERIFY(*std::max_element(widget.levels().begin(),
                                widget.levels().end()) > -10.0f);
  const SpectrumBands bands = SpectrumBands::fractionalOctave(80);
  const int loudest = loudestBand(widget.levels());
  QVERIFY(bands.edges[loudest] <= kToneHz &&
          kToneHz < bands.edges[loudest + 1]);

  // Saved: the next presenter starts with 80.
  QCOMPARE(QSettings().value("spectrum/bands").toInt(), 80);
  SpectrumWidget other;
  TestPlayerHost otherPlayer;
  SpectrumPresenter next(&other, otherPlayer, audio);
  QCOMPARE(other.levels().size(), size_t{80});
}

QTEST_MAIN(TestSpectrumPresenter)
#include "tst_spectrumpresenter.moc"
