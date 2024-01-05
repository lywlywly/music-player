#include <QObject>
#include <QTest>

#include "../lyricsloader.h"

class TestLyrics : public QObject {
  Q_OBJECT
 public:
  explicit TestLyrics(QObject *parent = nullptr);
  LyricsLoader lyricsHandler;

 signals:
 private slots:
  void testLoadLyricsFile1();
  void testLoadLyricsFile2();
  void testLoadLyricsFile3();
  void testMap();
};

TestLyrics::TestLyrics(QObject *parent) : QObject{parent} {}

void TestLyrics::testLoadLyricsFile1() {
  QMap<int, QString> lyricsMap =
      lyricsHandler.getLyrics("みちしるべ", "茅原実里");
  QCOMPARE(lyricsMap.keys()[0], 0);
  QCOMPARE(lyricsMap.keys()[1], 780);
  QCOMPARE(lyricsMap.keys()[10], 78070);
  QCOMPARE(lyricsMap.values()[0], " 作词 : 茅原実里");
  qDebug() << lyricsMap;

  QList<int> keys = lyricsMap.keys();

  qint64 progress = 12323;

  int progress2 = static_cast<int>(progress);
  auto it0 =
      std::find_if(keys.rbegin(), keys.rend(),
                   [progress, progress2](int num) { return num <= 12323; });
  qDebug() << *it0;
}

void TestLyrics::testLoadLyricsFile2() {
  QMap<int, QString> lyricsMap =
      lyricsHandler.getLyrics("社会距離", "40mP feat. 可不");
  QCOMPARE(lyricsMap.keys()[10], 37350);
  QCOMPARE(lyricsMap.values()[0], " 作词 : 40mP");
}

void TestLyrics::testLoadLyricsFile3()
{
  QMap<int, QString> lyricsMap =
      lyricsHandler.getLyrics("海阔天空", "BEYOND");
  QCOMPARE(lyricsMap.keys()[20], 59310);
  QCOMPARE(lyricsMap.values()[1], "词：黄家驹");
}

void TestLyrics::testMap() {
  QMap<int, QString> myMap;
  const QMap<int, QString> &mapref = myMap;
  myMap[1] = "One";
  myMap[2] = "Two";
  myMap[3] = "Three";
  mapref[1] = "123";
  qDebug() << myMap;
}

QTEST_MAIN(TestLyrics)
#include "tst_lyrics.moc"
