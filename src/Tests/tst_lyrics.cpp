#include <QDir>
#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "../lyricsloader.h"
#include "../lyricsmanager.h"

namespace {
#ifdef Q_OS_WIN
constexpr const char *kHomeEnvVar = "USERPROFILE";
#else
constexpr const char *kHomeEnvVar = "HOME";
#endif

// Points the home directory used by LyricsLoader::getLyrics at a temporary
// directory for the lifetime of this object.
class ScopedHomeDir {
public:
  explicit ScopedHomeDir(const QString &path)
      : hadValue_(qEnvironmentVariableIsSet(kHomeEnvVar)),
        oldValue_(qgetenv(kHomeEnvVar)) {
    qputenv(kHomeEnvVar, QDir::toNativeSeparators(path).toLocal8Bit());
  }
  ~ScopedHomeDir() {
    if (hadValue_) {
      qputenv(kHomeEnvVar, oldValue_);
    } else {
      qunsetenv(kHomeEnvVar);
    }
  }

private:
  bool hadValue_;
  QByteArray oldValue_;
};

std::map<int, std::string> sampleLyrics() {
  return {{1000, "first"}, {2500, "second"}, {4000, "third"}};
}

QList<int> emittedIndices(const QSignalSpy &spy) {
  QList<int> indices;
  for (const QList<QVariant> &args : spy) {
    indices.push_back(args.at(0).toInt());
  }
  return indices;
}
} // namespace

class TestLyrics : public QObject {
  Q_OBJECT

private slots:
  void parseLyricsText_parsesTimestampedLines();
  void parseLyricsText_scalesFractionByDigitCount();
  void parseLyricsText_skipsInvalidLines();
  void getLyrics_readsLrcFileFromMusicFolder();
  void getLyrics_missingFileReturnsEmpty();
  void onPlayerProgressChange_emitsLineIndexOnlyWhenItChanges();
  void setLyrics_resetsCurrentLine();
};

void TestLyrics::parseLyricsText_parsesTimestampedLines() {
  // Metadata tags are skipped, one line may carry several timestamps, CRLF
  // endings are stripped, and an empty text line is kept (it clears the
  // displayed line during instrumental gaps).
  const std::string raw = "[ti:Song]\r\n"
                          "[ar:Artist]\r\n"
                          "[00:01.00]line 1\r\n"
                          "[00:02.50][00:03.00]line 2\r\n"
                          "[01:05.20]\r\n";
  const std::map<int, std::string> lyrics = LyricsLoader::parseLyricsText(raw);

  const std::map<int, std::string> expected = {
      {1000, "line 1"}, {2500, "line 2"}, {3000, "line 2"}, {65200, ""}};
  QCOMPARE(lyrics, expected);
}

void TestLyrics::parseLyricsText_scalesFractionByDigitCount() {
  // Taggers write 1-, 2- or 3-digit fractions, sometimes mixed in one text.
  const std::string raw = "[00:01.5]tenths\n"
                          "[00:02.45]hundredths\n"
                          "[00:03.123]milliseconds\n"
                          "[01:00.007]leading zeros\n";
  const std::map<int, std::string> lyrics = LyricsLoader::parseLyricsText(raw);

  const std::map<int, std::string> expected = {{1500, "tenths"},
                                               {2450, "hundredths"},
                                               {3123, "milliseconds"},
                                               {60007, "leading zeros"}};
  QCOMPARE(lyrics, expected);
}

void TestLyrics::parseLyricsText_skipsInvalidLines() {
  const std::string raw = "[00:xx.xx]bad seconds\n"
                          "[00:01.ab]bad fraction\n"
                          "[00:01.1234]fraction too long\n"
                          "[00:01.]empty fraction\n"
                          "no timestamp\n";
  QVERIFY(LyricsLoader::parseLyricsText(raw).empty());
}

void TestLyrics::getLyrics_readsLrcFileFromMusicFolder() {
  QTemporaryDir home;
  QVERIFY(home.isValid());
  QVERIFY(QDir(home.path()).mkpath("Music"));
  QFile file(QDir(home.path()).filePath("Music/Artist - Title.lrc"));
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("[ti:Title]\n[00:01.00]hello\n[00:02.00]world\n");
  file.close();

  const ScopedHomeDir scopedHome(home.path());
  const std::map<int, std::string> lyrics =
      LyricsLoader::getLyrics("Title", "Artist");

  const std::map<int, std::string> expected = {{1000, "hello"},
                                               {2000, "world"}};
  QCOMPARE(lyrics, expected);
}

void TestLyrics::getLyrics_missingFileReturnsEmpty() {
  QTemporaryDir home;
  QVERIFY(home.isValid());

  const ScopedHomeDir scopedHome(home.path());
  QVERIFY(LyricsLoader::getLyrics("Title", "Artist").empty());
}

void TestLyrics::onPlayerProgressChange_emitsLineIndexOnlyWhenItChanges() {
  LyricsManager manager;
  manager.setLyrics(sampleLyrics());
  QSignalSpy spy(&manager, &LyricsManager::newLyricsLineIndex);

  manager.onPlayerProgressChange(500);  // before the first line: still -1
  manager.onPlayerProgressChange(1000); // line 0 starts
  manager.onPlayerProgressChange(1500); // still line 0
  manager.onPlayerProgressChange(2500); // line 1
  manager.onPlayerProgressChange(9000); // past the end: last line
  manager.onPlayerProgressChange(200);  // seek back before the first line

  QCOMPARE(emittedIndices(spy), (QList<int>{0, 1, 2, -1}));
}

void TestLyrics::setLyrics_resetsCurrentLine() {
  LyricsManager manager;
  manager.setLyrics(sampleLyrics());
  QSignalSpy spy(&manager, &LyricsManager::newLyricsLineIndex);

  manager.onPlayerProgressChange(2500);
  manager.setLyrics(sampleLyrics());
  manager.onPlayerProgressChange(2500);

  QCOMPARE(emittedIndices(spy), (QList<int>{1, 1}));
}

QTEST_MAIN(TestLyrics)
#include "tst_lyrics.moc"
