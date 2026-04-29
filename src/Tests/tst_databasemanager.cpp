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
  void createsPlayStatsSchema();
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
  QVERIFY(actualColumns.contains("identity_id"));
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

void TestDatabaseManager::createsPlayStatsSchema() {
  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("SELECT name FROM sqlite_master WHERE type='table' AND "
                 "name='song_play_stats'"));
  QVERIFY(q.next());

  QVERIFY(q.exec("PRAGMA table_info(song_play_stats)"));
  QSet<QString> columns;
  bool identityIsPrimaryKey = false;
  while (q.next()) {
    const QString name = q.value(1).toString();
    columns.insert(name);
    if (name == "identity_id" && q.value(5).toInt() == 1) {
      identityIsPrimaryKey = true;
    }
  }
  QVERIFY(columns.contains("identity_id"));
  QVERIFY(columns.contains("play_count"));
  QVERIFY(columns.contains("last_played_timestamp"));
  QVERIFY(identityIsPrimaryKey);
}

void TestDatabaseManager::
    deleteSong_cascadesToPlaylistItemsAndSongAttributes() {
  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  QVERIFY(q.exec("INSERT INTO playlists(playlist_id, name, last_played) VALUES "
                 "(1, 'P1', 1)"));
  QVERIFY(q.exec("INSERT INTO song_identities(identity_id, song_identity_key) "
                 "VALUES (1, 'song|artist|album')"));
  QVERIFY(q.exec("INSERT INTO songs(song_id, title, filepath, identity_id) "
                 "VALUES (10, 'Song', '/tmp/cascade.mp3', 1)"));
  QVERIFY(q.exec("INSERT INTO playlist_items(playlist_id, song_id, position) "
                 "VALUES (1, 10, 1)"));
  QVERIFY(q.exec(
      "INSERT INTO song_attributes(song_id, key, value_text, value_type) "
      "VALUES (10, 'rating', '8', 'number')"));
  QVERIFY(q.exec("INSERT INTO song_computed_attributes(song_id, key, "
                 "value_text, value_type) "
                 "VALUES (10, 'era', 'classic', 'text')"));
  QVERIFY(q.exec("INSERT INTO song_play_stats(identity_id, play_count, "
                 "last_played_timestamp) VALUES (1, 3, 1700000000)"));

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

  QVERIFY(q.exec("SELECT COUNT(*) FROM song_play_stats WHERE identity_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);
}

QTEST_MAIN(TestDatabaseManager)
#include "tst_databasemanager.moc"
