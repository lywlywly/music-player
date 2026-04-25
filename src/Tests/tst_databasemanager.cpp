#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"

class TestDatabaseManager : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void createsSongsSchemaFromRegistry();
  void createsComputedAttributesSchema();
  void deleteSong_cascadesToPlaylistItemsAndSongAttributes();

private:
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  QString connectionName_;
};

void TestDatabaseManager::init() {
  connectionName_ =
      QStringLiteral("test_databasemanager_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
}

void TestDatabaseManager::cleanup() {
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
}

void TestDatabaseManager::createsSongsSchemaFromRegistry() {
  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("PRAGMA table_info(songs)"));

  QSet<QString> actualColumns;
  while (q.next()) {
    actualColumns.insert(q.value(1).toString());
  }

  QVERIFY(actualColumns.contains("song_id"));
  for (const QString &id : registry_->songAttributeColumnIds()) {
    QVERIFY2(actualColumns.contains(id),
             qPrintable(QString("Missing songs column: %1").arg(id)));
  }
  QVERIFY(!actualColumns.contains("status"));
}

void TestDatabaseManager::createsComputedAttributesSchema() {
  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("SELECT name FROM sqlite_master WHERE type='table' AND "
                 "name='computed_attribute_definitions'"));
  QVERIFY(q.next());

  QVERIFY(q.exec("SELECT name FROM sqlite_master WHERE type='table' AND "
                 "name='song_computed_attributes'"));
  QVERIFY(q.next());
}

void TestDatabaseManager::
    deleteSong_cascadesToPlaylistItemsAndSongAttributes() {
  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  QVERIFY(q.exec("INSERT INTO playlists(playlist_id, name, last_played) VALUES "
                 "(1, 'P1', 1)"));
  QVERIFY(q.exec("INSERT INTO songs(song_id, title, filepath) VALUES (10, "
                 "'Song', '/tmp/cascade.mp3')"));
  QVERIFY(q.exec("INSERT INTO playlist_items(playlist_id, song_id, position) "
                 "VALUES (1, 10, 1)"));
  QVERIFY(q.exec(
      "INSERT INTO song_attributes(song_id, key, value_text, value_type) "
      "VALUES (10, 'rating', '8', 'number')"));
  QVERIFY(q.exec("INSERT INTO song_computed_attributes(song_id, key, "
                 "value_text, value_type) "
                 "VALUES (10, 'era', 'classic', 'text')"));

  QVERIFY(q.exec("DELETE FROM songs WHERE song_id=10"));

  QVERIFY(q.exec("SELECT COUNT(*) FROM playlist_items WHERE song_id=10"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);

  QVERIFY(q.exec("SELECT COUNT(*) FROM song_attributes WHERE song_id=10"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);

  QVERIFY(
      q.exec("SELECT COUNT(*) FROM song_computed_attributes WHERE song_id=10"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

QTEST_MAIN(TestDatabaseManager)
#include "tst_databasemanager.moc"
