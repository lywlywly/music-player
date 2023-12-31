#include <QObject>
#include <QTest>

#include "../songparser.h"

class TestParser : public QObject {
  Q_OBJECT
 public:
  explicit TestParser(QObject *parent = nullptr);
  SongParser parser;

 signals:
 private slots:
  void testLocalFile1();
  void testLocalFile2();
};

TestParser::TestParser(QObject *parent) : QObject{parent} {}
void TestParser::testLocalFile1() {
  Song song =
      parser.parseFile(QUrl{"/home/luyao/Music/ACAね、Rin音、Yaffle - "
                            "Character - 00 - Character.mp3"});
  QVERIFY(song.title == "Character");
}

void TestParser::testLocalFile2() {
  Song song = parser.parseFile(
      QUrl{"/home/luyao/Music/YOASOBI - ハルカ - 00 - ハルカ.mp3"});
  QVERIFY(song.artist == "YOASOBI");
}

QTEST_MAIN(TestParser)
#include "tst_parser.moc"
