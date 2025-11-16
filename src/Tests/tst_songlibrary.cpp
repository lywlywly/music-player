#include <QObject>
#include <QTest>

#include "../songlibrary.h"
#include "../songstore.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>

class TestSongLibrary : public QObject {
  Q_OBJECT
public:
  explicit TestSongLibrary(QObject *parent = nullptr);

signals:
private slots:
  void test1();
  void test2();
  void test3();
  void test4();
  void test5();
  void test6();
  void test7();
};

TestSongLibrary::TestSongLibrary(QObject *parent) : QObject{parent} {}

void TestSongLibrary::test1() {
  SongLibrary lb;
  SongStore s{lb, 1};

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

void TestSongLibrary::test2() {
  SongLibrary lb;
  SongStore s{lb, 1};

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

void TestSongLibrary::test3() {
  SongLibrary lb;
  SongStore s{lb, 1};

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

void TestSongLibrary::test4() {
  SongLibrary lb;

  lb.addTolibrary({
      {"title", "绝不认输"},
      {"artist", "张顺飞"},
      {"bpm", "130"},
      {"path", "f8fq"},
  });
  lb.addTolibrary({
      {"title", "相遇天使"},
      {"artist", "侯国玉"},
      {"bpm", "12306"},
      {"path", "g8fq"},
  });
  lb.addTolibrary({
      {"title", "狂浪"},
      {"artist", "张顺飞"},
      {"bpm", "135"},
      {"path", "f8fq2"},
  });

  std::unordered_set<std::string> set = lb.queryField("artist");
  std::unordered_set<std::string> expected = {"侯国玉", "张顺飞"};
  QCOMPARE(set, expected);
}

void TestSongLibrary::test5() {
  SongLibrary lb;

  lb.addTolibrary({
      {"title", "绝不认输"},
      {"artist", "张顺飞"},
      {"bpm", "130"},
      {"path", "f8fq"},
  });
  lb.addTolibrary({
      {"title", "相遇天使"},
      {"artist", "侯国玉"},
      {"bpm", "12306"},
      {"path", "g8fq"},
  });
  lb.addTolibrary({
      {"title", "狂浪"},
      {"artist", "张顺飞"},
      {"bpm", "135"},
      {"path", "f8fq2"},
  });

  const std::unordered_set<std::string> &set = lb.registerQueryField("artist");
  // lb.unRegisterQueryField("artist"); // should fail
  lb.addTolibrary({
      {"title", "告白气球"},
      {"artist", "大司"},
      {"bpm", "140"},
      {"path", "宝石青楼"},
  });
  std::unordered_set<std::string> expected = {"侯国玉", "张顺飞", "大司"};
  QCOMPARE(set, expected);
}

void TestSongLibrary::test6() {
  SongLibrary lb;

  lb.addTolibrary({
      {"title", "绝不认输"},
      {"artist", "张顺飞"},
      {"bpm", "130"},
      {"path", "f8fq"},
  });
  lb.addTolibrary({
      {"title", "相遇天使"},
      {"artist", "侯国玉"},
      {"bpm", "12306"},
      {"path", "g8fq"},
  });
  lb.addTolibrary({
      {"title", "狂浪"},
      {"artist", "张顺飞"},
      {"bpm", "135"},
      {"path", "f8fq2"},
  });

  const std::vector<int> &vec = lb.registerQuery("张顺飞");
  // lb.unregisterQuery("张顺飞"); // should fail
  lb.addTolibrary({
      {"title", "相信自己"},
      {"artist", "张顺飞"},
      {"bpm", "140"},
      {"path", "f8fq3"},
  });
  std::vector<int> expected = {0, 2, 3};
  QCOMPARE(vec, expected);
}

void TestSongLibrary::test7() {
  QSqlDatabase testDb = QSqlDatabase::addDatabase("QSQLITE", "test_connection");
  testDb.setDatabaseName(":memory:");
  testDb.open();

  QSqlQuery q(testDb);
  q.exec("CREATE TABLE songs (song_id INTEGER PRIMARY KEY AUTOINCREMENT, title "
         "TEXT NOT NULL, artist TEXT, album TEXT, filepath TEXT NOT NULL "
         "UNIQUE);");
  q.exec("INSERT INTO songs VALUES (1, 'SongA', 'artist a', 'album a', "
         "'/path/a.mp3')");
  q.exec("INSERT INTO songs VALUES (5, 'SongB', 'artist b', 'album b', "
         "'/path/b.mp3')");
  q.exec("INSERT INTO songs VALUES (8, '绝不认输', '张顺飞', '顺歌', "
         "'/path/绝不认输.mp3')");
  q.exec("INSERT INTO songs VALUES (88, '相遇天使', '侯国玉', '吉吉金曲', "
         "'/path/相遇天使.mp3')");
  q.exec("INSERT INTO songs VALUES (3, '正方形的腮帮', '大司', '老羁老羁', "
         "'/path/正方形的腮帮.mp3')");
  q.exec("CREATE TABLE playlist_items (playlist_item_id INTEGER PRIMARY "
         "KEY AUTOINCREMENT, playlist_id INTEGER NOT NULL, song_id "
         "INTEGER NOT NULL, position INTEGER NOT NULL, FOREIGN KEY "
         "(song_id) REFERENCES songs(song_id) ON DELETE CASCADE);");

  SongLibrary lb;
  SongStore s{lb, 1};
  lb.loadAll(testDb);

  q.exec(R"(
        SELECT *
        FROM songs
    )");

  QSqlRecord rec = q.record();
  int colCount = rec.count();

  while (q.next()) {
    for (int i = 0; i < colCount; ++i) {
      QString name = rec.fieldName(i);
      QVariant value = q.value(i);

      qDebug() << name << ":" << value;
    }
    qDebug() << "----";
  }

  MSong expected = {
      {"title", "SongA"},
      {"artist", "artist a"},
      {"album", "album a"},
      {"path", "/path/a.mp3"},
  };
  QCOMPARE(lb.getSongByPK(1), expected);
  expected = {
      {"title", "SongB"},
      {"artist", "artist b"},
      {"album", "album b"},
      {"path", "/path/b.mp3"},
  };
  QCOMPARE(lb.getSongByPK(5), expected);
  expected = {
      {"title", "相遇天使"},
      {"artist", "侯国玉"},
      {"album", "吉吉金曲"},
      {"path", "/path/相遇天使.mp3"},
  };
  QCOMPARE(lb.getSongByPK(88), expected);

  q.exec("INSERT INTO playlist_items(playlist_id, song_id, position) "
         "VALUES(1, ?, ?)");
  q.addBindValue(88);
  q.addBindValue(1);
  QVERIFY(q.exec());

  q.addBindValue(3);
  q.addBindValue(2);
  QVERIFY(q.exec());

  q.addBindValue(8);
  q.addBindValue(3);
  QVERIFY(q.exec());

  s.loadAll(testDb);

  QVERIFY(s.getIndexByPk(88) == 0);
  QVERIFY(s.getIndexByPk(3) == 1);
  QVERIFY(s.getIndexByPk(8) == 2);
  QVERIFY(s.getPkByIndex(0) == 88);
  QVERIFY(s.getPkByIndex(1) == 3);
  QVERIFY(s.getPkByIndex(2) == 8);
  expected = {
      {"title", "相遇天使"},
      {"artist", "侯国玉"},
      {"album", "吉吉金曲"},
      {"path", "/path/相遇天使.mp3"},
  };
  QCOMPARE(s.getSongByPk(88), expected);
  QCOMPARE(s.getSongByIndex(0), expected);
  expected = {
      {"title", "正方形的腮帮"},
      {"artist", "大司"},
      {"album", "老羁老羁"},
      {"path", "/path/正方形的腮帮.mp3"},
  };
  QCOMPARE(s.getSongByPk(3), expected);
  QCOMPARE(s.getSongByIndex(1), expected);
  expected = {
      {"title", "绝不认输"},
      {"artist", "张顺飞"},
      {"album", "顺歌"},
      {"path", "/path/绝不认输.mp3"},
  };
  QCOMPARE(s.getSongByPk(8), expected);
  QCOMPARE(s.getSongByIndex(2), expected);
}

QTEST_MAIN(TestSongLibrary)
#include "tst_songlibrary.moc"
