#include <QObject>
#include <QTest>

#include "../songparser.h"

class TestParser : public QObject {
  Q_OBJECT
public:
  explicit TestParser(QObject *parent = nullptr);

signals:
private slots:
  void testLocalFile1();
  void testLocalFile2();
  void testLocalOgg1();
  void testLocalFlac1();
};

TestParser::TestParser(QObject *parent) : QObject{parent} {}
void TestParser::testLocalFile1() {
  Song song =
      SongParser::parseFile(QUrl{"/home/luyao/Music/ACAね、Rin音、Yaffle - "
                                 "Character - 00 - Character.mp3"});
  QVERIFY(song.title == "Character");
}

void TestParser::testLocalFile2() {
  Song song = SongParser::parseFile(
      QUrl{"/home/luyao/Music/YOASOBI - ハルカ - 00 - ハルカ.mp3"});
  QVERIFY(song.artist == "YOASOBI");
}

void TestParser::testLocalOgg1() {
  Song song = SongParser::parseFile(
      QUrl{"/home/luyao/data/Music/New/40mP feat. 可不 - KAF+YOU KAFU "
           "COMPILATION ALBUM シンメトリー - 00 - 社会距離.ogg"});
  QVERIFY(song.artist == "40mP feat. 可不");
}

void TestParser::testLocalFlac1() {
  Song song = SongParser::parseFile(
      QUrl{"/home/luyao/data/Music/New/高田憂希寿美菜子 - "
           "TVアニメ「やがて君になる」エンディングテーマ「hect"
           "opascal」 - 02 - 好き、以外の言葉で.flac"});
  QVERIFY(song.artist == "高田憂希;寿美菜子");
}

QTEST_MAIN(TestParser)
#include "tst_parser.moc"
