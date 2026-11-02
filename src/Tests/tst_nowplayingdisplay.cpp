#include <QObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "../columnregistry.h"
#include "../displayexpression.h"
#include "../nowplayingdisplay.h"
#include "testaudiofiles.h"

namespace {
using Status = PlaybackQueue::PlaybackStatus;

// The default filepath does not exist, so the song is not detected as
// constant bitrate.
MSong makeSong(const std::string &bitrateKbps,
               const std::string &filepath = "/nonexistent/myplayer.flac") {
  MSong song;
  song.insert_or_assign("artist", FieldValue("Artist", "artist"));
  song.insert_or_assign("title", FieldValue("Title", "title"));
  song.insert_or_assign("filepath", FieldValue(filepath, "filepath"));
  if (!bitrateKbps.empty()) {
    song.insert_or_assign("bitrate", FieldValue(bitrateKbps, "bitrate"));
  }
  return song;
}

void setStatusBarExpression(const QString &expression) {
  saveDisplayExpressionSetting(DisplayExpressionKind::StatusBar, expression);
}
} // namespace

class TestNowPlayingDisplay : public QObject {
  Q_OBJECT

private slots:
  void init();
  void stopped_showsNoStatusAndIdleTitle();
  void playing_evaluatesDefaultExpressions();
  void bitrate_prefersStreamForVariableBitrateFiles();
  void bitrate_prefersTagForConstantBitrateFiles();
  void stopSong_forgetsBitrate();
  void runtimeSymbols_followPassedStatus();
  void constructor_loadsExpressionsFromSettings();
  void setExpression_replacesOneKindAndFallsBackOnParseError();
  void loadExpressions_resolvesColumnsAddedLater();

private:
  ColumnRegistry registry_;
};

void TestNowPlayingDisplay::init() { QSettings().clear(); }

void TestNowPlayingDisplay::stopped_showsNoStatusAndIdleTitle() {
  NowPlayingDisplay display(registry_);
  const MSong song = makeSong("320");
  display.startSong(song);

  QCOMPARE(display.statusBarText(Status::None, &song), QString());
  QCOMPARE(display.windowTitle(Status::None, &song, QStringLiteral("idle")),
           QStringLiteral("idle"));
}

void TestNowPlayingDisplay::playing_evaluatesDefaultExpressions() {
  NowPlayingDisplay display(registry_);
  const MSong song = makeSong("320");
  display.startSong(song);
  display.setDurationMs(125000);
  display.setPositionMs(65000);

  QCOMPARE(display.windowTitle(Status::Playing, &song, QStringLiteral("idle")),
           QStringLiteral("Artist - Title"));
  const QString status = display.statusBarText(Status::Playing, &song);
  QVERIFY2(status.contains("320 kbps"), qPrintable(status));
  QVERIFY2(status.contains("01:05 / 02:05"), qPrintable(status));
}

void TestNowPlayingDisplay::bitrate_prefersStreamForVariableBitrateFiles() {
  setStatusBarExpression(QStringLiteral("`${bitrate}`"));
  NowPlayingDisplay display(registry_);
  const MSong song = makeSong("320");
  display.startSong(song);

  // Tag bitrate until the stream reports one.
  QCOMPARE(display.statusBarText(Status::Playing, &song),
           QStringLiteral("320"));
  QVERIFY(display.setStreamBitrateBps(256000));
  QCOMPARE(display.statusBarText(Status::Playing, &song),
           QStringLiteral("256"));
  // A negative report is treated as unknown.
  QVERIFY(display.setStreamBitrateBps(-1));
  QCOMPARE(display.statusBarText(Status::Playing, &song),
           QStringLiteral("320"));

  // A new song drops the previous stream bitrate.
  QVERIFY(display.setStreamBitrateBps(256000));
  const MSong untagged = makeSong("");
  display.startSong(untagged);
  QCOMPARE(display.statusBarText(Status::Playing, &untagged),
           QStringLiteral("0"));
}

void TestNowPlayingDisplay::bitrate_prefersTagForConstantBitrateFiles() {
  setStatusBarExpression(QStringLiteral("`${bitrate}`"));
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString wav = dir.filePath("cbr.wav");
  QVERIFY(writeSilentWav(wav));
  NowPlayingDisplay display(registry_);
  const MSong song = makeSong("320", wav.toStdString());
  display.startSong(song);

  QVERIFY(!display.setStreamBitrateBps(256000));
  QCOMPARE(display.statusBarText(Status::Playing, &song),
           QStringLiteral("320"));

  // Without a tag bitrate the stream bitrate is shown, even for a CBR file.
  const MSong untagged = makeSong("", wav.toStdString());
  display.startSong(untagged);
  QVERIFY(display.setStreamBitrateBps(256000));
  QCOMPARE(display.statusBarText(Status::Playing, &untagged),
           QStringLiteral("256"));
}

void TestNowPlayingDisplay::stopSong_forgetsBitrate() {
  setStatusBarExpression(QStringLiteral("`${bitrate}`"));
  NowPlayingDisplay display(registry_);
  const MSong song = makeSong("320");
  display.startSong(song);
  display.setStreamBitrateBps(256000);

  display.stopSong();
  QCOMPARE(display.statusBarText(Status::Playing, &song), QStringLiteral("0"));
}

void TestNowPlayingDisplay::runtimeSymbols_followPassedStatus() {
  NowPlayingDisplay display(registry_);
  display.setPositionMs(65500);
  display.setDurationMs(125000);

  const StatusRuntimeSymbolTable paused =
      display.runtimeSymbols(Status::Paused);
  QCOMPARE(paused.fieldValue("status:ispaused")->text, std::string("true"));
  QCOMPARE(paused.fieldValue("status:isplaying")->text, std::string("false"));
  QCOMPARE(paused.fieldValue("status:playback_time")->text, std::string("65"));
  QCOMPARE(paused.fieldValue("status:duration")->text, std::string("125"));

  const StatusRuntimeSymbolTable playing =
      display.runtimeSymbols(Status::Playing);
  QCOMPARE(playing.fieldValue("status:isplaying")->text, std::string("true"));
}

void TestNowPlayingDisplay::constructor_loadsExpressionsFromSettings() {
  setStatusBarExpression(QStringLiteral("'status'"));
  saveDisplayExpressionSetting(DisplayExpressionKind::WindowTitle,
                               QStringLiteral("'title'"));
  NowPlayingDisplay display(registry_);
  QCOMPARE(display.statusBarText(Status::Playing, nullptr),
           QStringLiteral("status"));
  QCOMPARE(display.windowTitle(Status::Playing, nullptr, QString()),
           QStringLiteral("title"));
}

void TestNowPlayingDisplay::
    setExpression_replacesOneKindAndFallsBackOnParseError() {
  NowPlayingDisplay display(registry_);
  const MSong song = makeSong("320");
  const QString defaultStatus = display.statusBarText(Status::Playing, &song);

  display.setExpression(DisplayExpressionKind::WindowTitle,
                        QStringLiteral("'custom'"));
  QCOMPARE(display.windowTitle(Status::Playing, &song, QString()),
           QStringLiteral("custom"));
  QCOMPARE(display.statusBarText(Status::Playing, &song), defaultStatus);

  display.setExpression(DisplayExpressionKind::StatusBar,
                        QStringLiteral("'custom'"));
  QCOMPARE(display.statusBarText(Status::Playing, &song),
           QStringLiteral("custom"));
  display.setExpression(DisplayExpressionKind::StatusBar,
                        QStringLiteral("no_such_field IS 1"));
  QCOMPARE(display.statusBarText(Status::Playing, &song), defaultStatus);
}

void TestNowPlayingDisplay::loadExpressions_resolvesColumnsAddedLater() {
  setStatusBarExpression(QStringLiteral("`[${mood}] ${title}`"));
  ColumnRegistry registry; // no custom columns yet, as at startup
  NowPlayingDisplay display(registry);
  MSong song = makeSong("320");
  song.insert_or_assign("attr:mood", FieldValue("happy", "attr:mood"));
  // An unknown name inside `${}` is parsed as plain text.
  QCOMPARE(display.statusBarText(Status::Playing, &song),
           QStringLiteral("[mood] Title"));

  registry.addOrUpdateDynamicColumn({.id = "attr:mood", .title = "Mood"},
                                    {.id = "attr:mood",
                                     .source = ColumnSource::SongAttribute,
                                     .valueType = ValueType::Text,
                                     .searchable = true,
                                     .writable = true});
  display.loadExpressions();
  QCOMPARE(display.statusBarText(Status::Playing, &song),
           QStringLiteral("[happy] Title"));
}

QTEST_MAIN(TestNowPlayingDisplay)
#include "tst_nowplayingdisplay.moc"
