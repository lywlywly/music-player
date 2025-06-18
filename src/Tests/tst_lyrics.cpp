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
  std::map<int, std::string> lyricsMap =
      lyricsHandler.getLyrics("みちしるべ", "茅原実里");
  auto it = lyricsMap.begin();
  std::advance(it, 0);
  QCOMPARE(it->first, 0);
  QCOMPARE(it->second, " 作词 : 茅原実里");
  it = lyricsMap.begin();
  std::advance(it, 1);
  QCOMPARE(it->first, 780);
  it = lyricsMap.begin();
  std::advance(it, 10);
  QCOMPARE(it->first, 78070);
  qDebug() << lyricsMap;

  std::vector<int> keys;
  for (auto &[key, value] : lyricsMap) {
    keys.push_back(key);
  }

  qint64 progress = 12323;

  int progress2 = static_cast<int>(progress);
  auto it0 =
      std::find_if(keys.rbegin(), keys.rend(),
                   [progress, progress2](int num) { return num <= 12323; });
  qDebug() << *it0;
}

void TestLyrics::testLoadLyricsFile2() {
  std::map<int, std::string> lyricsMap =
      lyricsHandler.getLyrics("社会距離", "40mP feat. 可不");
  auto it = lyricsMap.begin();
  std::advance(it, 0);
  QCOMPARE(it->second, " 作词 : 40mP");
  std::advance(it, 10);
  QCOMPARE(it->first, 37350);
}

void TestLyrics::testLoadLyricsFile3() {
  std::map<int, std::string> lyricsMap =
      lyricsHandler.getLyrics("海阔天空", "BEYOND");
  auto it = lyricsMap.begin();
  std::advance(it, 1);
  QCOMPARE(it->second, "词：黄家驹");
  it = lyricsMap.begin();
  std::advance(it, 20);
  QCOMPARE(it->first, 59310);
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
