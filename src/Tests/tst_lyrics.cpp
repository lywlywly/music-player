#include <QObject>
#include <QTest>

#include "../lyricshandler.h"

class TestLyrics : public QObject {
  Q_OBJECT
 public:
  explicit TestLyrics(QObject *parent = nullptr);
  LyricsHandler lyricsHandler;

 signals:
 private slots:
  void testLoadLyricsFile();
  void testLoadLyricsFile2();
};

TestLyrics::TestLyrics(QObject *parent) : QObject{parent} {}

void TestLyrics::testLoadLyricsFile() {
  QMap<int, QString> lyricsMap =
      lyricsHandler.getLyrics("みちしるべ", "茅原実里");
  QCOMPARE(lyricsMap.keys()[0], 0);
  QCOMPARE(lyricsMap.keys()[1], 780);
  QCOMPARE(lyricsMap.keys()[10], 78070);
  QCOMPARE(lyricsMap.values()[0], " 作词 : 茅原実里");
}

void TestLyrics::testLoadLyricsFile2()
{
  QMap<int, QString> lyricsMap =
      lyricsHandler.getLyrics("社会距離", "40mP feat. 可不");
  QCOMPARE(lyricsMap.keys()[10], 41090);
  QCOMPARE(lyricsMap.values()[0], " 作词 : 40mP");
}

QTEST_MAIN(TestLyrics)
#include "tst_lyrics.moc"
