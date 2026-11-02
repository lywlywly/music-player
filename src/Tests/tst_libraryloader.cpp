#include <QObject>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../libraryloader.h"
#include "../songlibrary.h"

namespace {
QString uniqueConnectionName() {
  return QStringLiteral("libraryloader_test_%1")
      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

// Adds the custom column attr:mood and one song with mood "happy" to the
// database, through a registry and library of its own. Returns the song's pk.
int seedSongWithCustomField(DatabaseManager &databaseManager) {
  ColumnRegistry registry;
  const ColumnDefinition moodColumn{.id = "attr:mood", .title = "Mood"};
  const FieldDefinition moodField{.id = "attr:mood",
                                  .source = ColumnSource::SongAttribute,
                                  .valueType = ValueType::Text,
                                  .searchable = true,
                                  .writable = true};
  if (!registry.upsertCustomTagDefinition(databaseManager.db(), moodColumn,
                                          moodField) ||
      !registry.loadDynamicColumns(databaseManager.db())) {
    return -1;
  }
  MSong song;
  song.insert_or_assign("title", FieldValue("Song", "title"));
  song.insert_or_assign("artist", FieldValue("Artist", "artist"));
  song.insert_or_assign("album", FieldValue("Album", "album"));
  song.insert_or_assign("filepath", FieldValue("/tmp/loader.wav", "filepath"));
  song.insert_or_assign("song_identity_key",
                        FieldValue("song|artist|album", "song_identity_key"));
  song.insert_or_assign("attr:mood", FieldValue("happy", "attr:mood"));
  SongLibrary library(registry, databaseManager);
  return library.addTolibrary(std::move(song));
}
} // namespace

class TestLibraryLoader : public QObject {
  Q_OBJECT

private slots:
  void fileDatabase_loadsColumnsAndSongsFromEventLoop();
  void memoryDatabase_loadsBeforeReturning();
};

void TestLibraryLoader::fileDatabase_loadsColumnsAndSongsFromEventLoop() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString dbPath = dir.filePath("library.sqlite");
  int songPk = -1;
  {
    ColumnRegistry setupRegistry;
    const QString setupConnection = uniqueConnectionName();
    {
      DatabaseManager setupDatabase(setupRegistry, dbPath, setupConnection);
      songPk = seedSongWithCustomField(setupDatabase);
    }
    QSqlDatabase::removeDatabase(setupConnection);
  }
  QVERIFY(songPk >= 0);

  const QString connection = uniqueConnectionName();
  {
    ColumnRegistry registry;
    DatabaseManager databaseManager(registry, dbPath, connection);
    SongLibrary library(registry, databaseManager);
    QObject context;
    int loadedCount = 0;

    loadLibrary(registry, databaseManager, library, &context,
                [&loadedCount]() { ++loadedCount; });
    // Loads on the worker thread: nothing is applied yet.
    QCOMPARE(loadedCount, 0);
    QVERIFY(!registry.hasField("attr:mood"));

    QTRY_COMPARE(loadedCount, 1);
    QVERIFY(registry.hasField("attr:mood"));
    QCOMPARE(library.getSongByPK(songPk).at("attr:mood").text, "happy");
  }
  QSqlDatabase::removeDatabase(connection);
}

void TestLibraryLoader::memoryDatabase_loadsBeforeReturning() {
  const QString connection = uniqueConnectionName();
  {
    ColumnRegistry registry;
    DatabaseManager databaseManager(registry, QStringLiteral(":memory:"),
                                    connection);
    const int songPk = seedSongWithCustomField(databaseManager);
    QVERIFY(songPk >= 0);
    SongLibrary library(registry, databaseManager);
    QObject context;
    int loadedCount = 0;

    loadLibrary(registry, databaseManager, library, &context,
                [&loadedCount]() { ++loadedCount; });
    QCOMPARE(loadedCount, 1);
    QVERIFY(registry.hasField("attr:mood"));
    QCOMPARE(library.getSongByPK(songPk).at("attr:mood").text, "happy");
  }
  QSqlDatabase::removeDatabase(connection);
}

QTEST_MAIN(TestLibraryLoader)
#include "tst_libraryloader.moc"
