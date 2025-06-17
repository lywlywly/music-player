#include <QObject>
#include <QTest>

#include "../songlibrary.h"
#include "../songstore.h"

class TestSongLibrary : public QObject {
  Q_OBJECT
public:
  explicit TestSongLibrary(QObject *parent = nullptr);

signals:
private slots:
  void test2();
  void test3();
  void test4();
};

TestSongLibrary::TestSongLibrary(QObject *parent) : QObject{parent} {}

void TestSongLibrary::test2() {
  SongLibrary lb;
  SongStore s{lb};

  const std::vector<int> &view = s.getSongsView();

  s.addSong({
      {"title", "绝不认输"},
      {"artist", "张顺飞"},
      {"bpm", "130"},
      {"path", "f8fq"},
  });
  s.addSong({
      {"title", "相遇天使"},
      {"artist", "侯国玉"},
      {"bpm", "12306"},
      {"path", "g8fq"},
  });
  s.addSong({
      {"title", "狂浪"},
      {"artist", "张顺飞"},
      {"bpm", "135"},
      {"path", "f8fq2"},
  });

  for (const auto p : view) {
    qDebug() << s.getSongByPk(p);
  }

  std::vector<int> allsongs = lb.query("张顺飞");
  std::vector<MSong> actual;

  for (const int p : allsongs) {
    actual.push_back(lb.getSongByPK(p));
  }

  std::vector<MSong> expected = {
      {
          {"title", "绝不认输"},
          {"artist", "张顺飞"},
          {"bpm", "130"},
          {"path", "f8fq"},
      },
      {
          {"title", "狂浪"},
          {"artist", "张顺飞"},
          {"bpm", "135"},
          {"path", "f8fq2"},
      },

  };

  QCOMPARE(actual, expected);
}

void TestSongLibrary::test3() {
  SongLibrary lb;
  SongStore s{lb};

  const std::vector<int> &view = s.getSongsView();

  s.addSong({
      {"title", "绝不认输"},
      {"artist", "张顺飞"},
      {"bpm", "130"},
      {"path", "f8fq"},
  });
  s.addSong({
      {"title", "相遇天使"},
      {"artist", "侯国玉"},
      {"bpm", "12306"},
      {"path", "g8fq"},
  });
  s.addSong({
      {"title", "狂浪"},
      {"artist", "詹顺飞"},
      {"bpm", "135"},
      {"path", "f8fq2"},
  });

  s.sortByField("artist");
  qDebug() << view.size();
  for (const auto p : view) {
    qDebug() << p;
  }
  auto &indices = s.getIndices();
  qDebug() << indices;
}

void TestSongLibrary::test4() {
  SongLibrary lb;
  SongStore s{lb};

  const std::vector<int> &view = s.getSongsView();

  s.addSong({
      {"title", "绝不认输"},
      {"artist", "张顺飞"},
      {"bpm", "130"},
      {"path", "f8fq"},
  });
  s.addSong({
      {"title", "相遇天使"},
      {"artist", "侯国玉"},
      {"bpm", "12306"},
      {"path", "g8fq"},
  });
  s.addSong({
      {"title", "狂浪"},
      {"artist", "张顺飞"},
      {"bpm", "135"},
      {"path", "f8fq2"},
  });

  auto pk = view[2];
  QVERIFY(pk == 2);
  auto song = s.getSongByPk(pk);
  MSong expected = {
      {"title", "狂浪"},
      {"artist", "张顺飞"},
      {"bpm", "135"},
      {"path", "f8fq2"},
  };
  qDebug() << song;
  QCOMPARE(song, expected);
}

QTEST_MAIN(TestSongLibrary)
#include "tst_songlibrary.moc"
