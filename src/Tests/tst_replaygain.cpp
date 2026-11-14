#include <QObject>
#include <QTest>
#include <cmath>

#include "../replaygain.h"

// The rules of GStreamer's rgvolume (gstrgvolume.c), and Off.
class TestReplayGain : public QObject {
  Q_OBJECT

private slots:
  void noTags_giveZeroInEveryMode();
  void off_givesZeroWithTags();
  void eachMode_usesItsGainAndFallsBackToTheOther();
  void missingPeak_cutsAPositiveGainToZero();
  void peak_limitsTheGainToFullScale();
  void peakAboveFullScale_isClampedAndBogusValuesIgnored();
  void gain_isCappedAtTwentyDb();
  void laterEvents_keepEarlierValues();
  void referenceLevel_movesPositiveDbSplOnly();
  void r128Gain_takesPriorityPlusFiveDbWithoutPeak();
};

namespace {
ReplayGainTags tagsOf(const ReplayGainTagValues &values) {
  ReplayGainTags tags;
  tags.merge(values);
  return tags;
}

bool near(double actual, double expected) {
  return std::abs(actual - expected) < 1e-9;
}
} // namespace

void TestReplayGain::noTags_giveZeroInEveryMode() {
  const ReplayGainTags tags;
  QCOMPARE(tags.gainDb(ReplayGainMode::Off), 0.0);
  QCOMPARE(tags.gainDb(ReplayGainMode::Track), 0.0);
  QCOMPARE(tags.gainDb(ReplayGainMode::Album), 0.0);
}

void TestReplayGain::off_givesZeroWithTags() {
  const ReplayGainTags tags = tagsOf({.trackGain = -4.0, .albumGain = -10.0});
  QCOMPARE(tags.gainDb(ReplayGainMode::Off), 0.0);
}

void TestReplayGain::eachMode_usesItsGainAndFallsBackToTheOther() {
  const ReplayGainTags both = tagsOf({.trackGain = -4.0, .albumGain = -10.0});
  QCOMPARE(both.gainDb(ReplayGainMode::Track), -4.0);
  QCOMPARE(both.gainDb(ReplayGainMode::Album), -10.0);

  const ReplayGainTags trackOnly = tagsOf({.trackGain = -4.0});
  QCOMPARE(trackOnly.gainDb(ReplayGainMode::Album), -4.0);

  const ReplayGainTags albumOnly = tagsOf({.albumGain = -10.0});
  QCOMPARE(albumOnly.gainDb(ReplayGainMode::Track), -10.0);
}

void TestReplayGain::missingPeak_cutsAPositiveGainToZero() {
  QCOMPARE(tagsOf({.trackGain = 3.0}).gainDb(ReplayGainMode::Track), 0.0);
}

void TestReplayGain::peak_limitsTheGainToFullScale() {
  // Peak 0.5 allows +6.02 dB.
  const double allowed = -20.0 * std::log10(0.5);
  QVERIFY(near(tagsOf({.trackGain = 9.0, .trackPeak = 0.5})
                   .gainDb(ReplayGainMode::Track),
               allowed));
  QCOMPARE(tagsOf({.trackGain = 3.0, .trackPeak = 0.5})
               .gainDb(ReplayGainMode::Track),
           3.0);
  // Each gain uses its own peak.
  const ReplayGainTags tags = tagsOf(
      {.trackGain = 9.0, .trackPeak = 1.0, .albumGain = 9.0, .albumPeak = 0.5});
  QCOMPARE(tags.gainDb(ReplayGainMode::Track), 0.0);
  QVERIFY(near(tags.gainDb(ReplayGainMode::Album), allowed));
}

void TestReplayGain::peakAboveFullScale_isClampedAndBogusValuesIgnored() {
  QCOMPARE(tagsOf({.trackGain = 3.0, .trackPeak = 1.2})
               .gainDb(ReplayGainMode::Track),
           0.0);
  // A gain outside +-60 dB, or a peak of 0, counts as missing.
  QCOMPARE(tagsOf({.trackGain = -70.0, .albumGain = -10.0})
               .gainDb(ReplayGainMode::Track),
           -10.0);
  QCOMPARE(tagsOf({.trackGain = 3.0, .trackPeak = 0.0})
               .gainDb(ReplayGainMode::Track),
           0.0);
}

void TestReplayGain::gain_isCappedAtTwentyDb() {
  QCOMPARE(tagsOf({.trackGain = 40.0, .trackPeak = 0.001})
               .gainDb(ReplayGainMode::Track),
           20.0);
}

void TestReplayGain::laterEvents_keepEarlierValues() {
  ReplayGainTags tags;
  tags.merge({.trackGain = -4.0});
  tags.merge({.albumGain = -10.0});
  tags.merge({}); // e.g. a bitrate update
  QCOMPARE(tags.gainDb(ReplayGainMode::Track), -4.0);
  QCOMPARE(tags.gainDb(ReplayGainMode::Album), -10.0);
  tags.merge({.trackGain = -2.0});
  QCOMPARE(tags.gainDb(ReplayGainMode::Track), -2.0);
}

void TestReplayGain::referenceLevel_movesPositiveDbSplOnly() {
  // 83 dB SPL (the 2001 proposal): +6 dB to ReplayGain's 89.
  QCOMPARE(tagsOf({.trackGain = -10.0, .referenceLevel = 83.0})
               .gainDb(ReplayGainMode::Track),
           -4.0);
  // LUFS (0 or below): unchanged.
  QCOMPARE(tagsOf({.trackGain = -10.0, .referenceLevel = -18.0})
               .gainDb(ReplayGainMode::Track),
           -10.0);
}

void TestReplayGain::r128Gain_takesPriorityPlusFiveDbWithoutPeak() {
  // The event's peak is not used with an R128 gain, so the cut to 0 dB
  // applies to a positive gain as for a missing peak.
  const ReplayGainTags tags =
      tagsOf({.trackGain = -4.0, .trackGainR128 = -12.0, .trackPeak = 0.5});
  QCOMPARE(tags.gainDb(ReplayGainMode::Track), -7.0);
  QCOMPARE(tagsOf({.trackGainR128 = -2.0, .trackPeak = 0.5})
               .gainDb(ReplayGainMode::Track),
           0.0);
}

QTEST_GUILESS_MAIN(TestReplayGain)
#include "tst_replaygain.moc"
