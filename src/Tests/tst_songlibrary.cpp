#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <numeric>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../songlibrary.h"
#include "testsymlink.h"

namespace {
// The form SongLibrary stores a missing file's path in: cleaned and absolute
// ("C:/tmp/..." on Windows). Refreshes look songs up by it.
std::string storedPath(const char *path) {
  return QDir::cleanPath(QFileInfo(QString::fromUtf8(path)).absoluteFilePath())
      .toStdString();
}

MSong makeSong(const QString &title, const QString &artist, const QString &path,
               const QString &album = {}, const QString &tracknumber = {},
               const QString &date = {}, const QString &genre = {}) {
  MSong song;
  song.insert_or_assign("title", FieldValue(title.toStdString(), "title"));
  song.insert_or_assign("artist", FieldValue(artist.toStdString(), "artist"));
  if (!album.isEmpty()) {
    song.insert_or_assign("album", FieldValue(album.toStdString(), "album"));
  }
  song.insert_or_assign("discnumber", FieldValue("1", "discnumber"));
  if (!tracknumber.isEmpty()) {
    song.insert_or_assign("tracknumber",
                          FieldValue(tracknumber.toStdString(), "tracknumber"));
  }
  if (!date.isEmpty()) {
    song.insert_or_assign("date", FieldValue(date.toStdString(), "date"));
  }
  if (!genre.isEmpty()) {
    song.insert_or_assign("genre", FieldValue(genre.toStdString(), "genre"));
  }
  song.insert_or_assign("filepath", FieldValue(path.toStdString(), "filepath"));
  return song;
}
// Above every pk the tests insert (the largest is 71).
constexpr int kPkBound = 1000;

// Search candidates covering every song of the tests' libraries: each pk
// below kPkBound, as search skips pks with no song.
std::vector<int> allPks() {
  std::vector<int> pks(kPkBound);
  std::iota(pks.begin(), pks.end(), 0);
  return pks;
}

// A provided source over rows in memory, as a plugin's provider: songs are
// grouped by lowercase title|artist, and each key has a "plays" number and a
// "played_at" date.
struct FakeProvider {
  ProvidedValuesByKey rows;
  // The keys of each values() call, sorted.
  QList<QStringList> valuesCalls;

  ProvidedFields fields() {
    return {.sourceId = "fake",
            .fields = {{.id = "plays", .type = ValueType::Number},
                       {.id = "played_at", .type = ValueType::DateTime}},
            .keyFields = {"title", "artist"},
            .songKey =
                [](const QStringList &keyFieldValues) {
                  return keyFieldValues.join('|').toLower();
                },
            .values =
                [this](const QStringList &keys) {
                  QStringList sorted = keys;
                  sorted.sort();
                  valuesCalls.push_back(sorted);
                  ProvidedValuesByKey values;
                  for (const QString &key : keys) {
                    if (rows.contains(key)) {
                      values.insert(key, rows.value(key));
                    }
                  }
                  return values;
                },
            .allValues = [this]() { return rows; }};
  }
};

// Registers FakeProvider's fields, as PluginManager does a plugin's.
void registerFakeFields(ColumnRegistry &registry) {
  registry.addPluginField({.id = "plays", .title = "Plays"},
                          {.id = "plays",
                           .source = ColumnSource::Plugin,
                           .valueType = ValueType::Number,
                           .writable = false});
  registry.addPluginField({.id = "played_at", .title = "Played At"},
                          {.id = "played_at",
                           .source = ColumnSource::Plugin,
                           .valueType = ValueType::DateTime,
                           .displayKind = DisplayKind::EpochSecondsDateTime,
                           .writable = false});
}

std::string fieldText(const SongLibrary &library, int pk, const char *field) {
  const MSong &song = library.getSongByPK(pk);
  const auto it = song.find(field);
  return it == song.end() ? std::string("<none>") : it->second.text;
}
} // namespace

class TestSongLibrary : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void addToLibrary_sameFilepathUpdatesExistingSong();
  void addToLibrary_canonicalisesFilepath();
  void loadFromDatabase_loadsBuiltInAndDynamicAttributes();
  void loadFromDatabase_assignsFieldIdsForAllLoadedFields();
  void loadFromDatabase_removesUnknownDynamicAttributes();
  void loadFromDatabase_fillsProvidedValuesByKey();
  void addToLibrary_refreshesProvidedValuesInOneBatch();
  void refreshStale_skipsRemovedSongsAndKeysNoSongHas();
  void retag_dropsOldValuesAndReadsTheNewKey();
  void load_keepsStaleKeysUntilReplaceWithSnapshot();
  void search_matchesCaseInsensitiveExact();
  void search_supportsAndOr();
  void search_supportsParentheses();
  void search_supportsHasForTagLibStyleMultiValueFields();
  void search_supportsInList();
  void search_supportsInRange();
  void search_supportsQuotedValues();
  void search_supportsNumericFieldComparison();
  void search_supportsRelationalComparisons();
  void search_relationalComparisonSkipsMissingDate();
  void search_providedDateTimeField_supportsEquality();
  void search_supportsNot();
  void search_supportsCustomFields();
  void search_supportsComputedFields();
  void search_noMatchReturnsEmpty();
  void search_onlyCandidatesInFilepathOrder();
  void refreshSongFromFile_usesInjectedParserAndSyncsDb();
  void refreshSongFromFile_populatesRemainingFields();
  void refreshSongFromFile_unparsableFileKeepsStoredFields();
  void refreshSongFromFile_customLyricsFieldsPersist();
  void refreshSongsFromFilepaths_dedupesAndReportsProgress();
  void registerQuery_tracksLaterInsertions();
  void queryHelpers_skipEmptySongSlots();
  void removeSongs_deletesSongAndAttributes();
  void removeSongs_updatesRegisteredQueries();

private:
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  QString connectionName_;
};

void TestSongLibrary::init() {
  connectionName_ =
      QStringLiteral("test_songlibrary_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
}

void TestSongLibrary::cleanup() {
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
}

void TestSongLibrary::addToLibrary_canonicalisesFilepath() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString root = QFileInfo(dir.path()).canonicalFilePath();
  QVERIFY(QDir().mkpath(root + "/music"));
  QFile file(root + "/music/a.mp3");
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.close();

  const int pk =
      library_->addTolibrary(makeSong("A", "Artist", root + "/music/a.mp3"));
  QCOMPARE(library_->addTolibrary(
               makeSong("A", "Artist", root + "/music/../music/a.mp3")),
           pk);
  if (makeSymlink(root + "/music", root + "/link")) {
    QCOMPARE(
        library_->addTolibrary(makeSong("A", "Artist", root + "/link/a.mp3")),
        pk);
  }
  QCOMPARE(
      QString::fromStdString(library_->getSongByPK(pk).at("filepath").text),
      root + "/music/a.mp3");

  // A missing file keeps its cleaned absolute path.
  const int missingPk =
      library_->addTolibrary(makeSong("X", "Artist", root + "/gone/../x.mp3"));
  QCOMPARE(QString::fromStdString(
               library_->getSongByPK(missingPk).at("filepath").text),
           root + "/x.mp3");
}

void TestSongLibrary::addToLibrary_sameFilepathUpdatesExistingSong() {
  registry_->addOrUpdateDynamicColumn({.id = "attr:rating",
                                       .title = "Rating",
                                       .sortable = true,
                                       .visibleByDefault = false,
                                       .defaultWidth = 120},
                                      {.id = "attr:rating",
                                       .source = ColumnSource::SongAttribute,
                                       .valueType = ValueType::Number,
                                       .displayKind = DisplayKind::Raw,
                                       .expression = "",
                                       .searchable = true,
                                       .writable = true});

  MSong first = makeSong("Old Title", "Artist A", "/tmp/song-a.mp3");
  first.insert_or_assign("attr:rating", FieldValue("3", "attr:rating"));
  const int firstId = library_->addTolibrary(std::move(first));

  MSong updated = makeSong("New Title", "Artist A", "/tmp/song-a.mp3");
  updated.insert_or_assign("attr:rating", FieldValue("5", "attr:rating"));
  const int secondId = library_->addTolibrary(std::move(updated));

  QCOMPARE(secondId, firstId);

  const MSong &song = library_->getSongByPK(firstId);
  QCOMPARE(song.at("title").text, std::string("New Title"));
  QVERIFY(song.contains("attr:rating"));
  QCOMPARE(song.at("attr:rating").text, std::string("5"));

  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  QVERIFY(q.exec("SELECT COUNT(*) FROM songs"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);

  q.prepare("SELECT title FROM songs WHERE song_id=:song_id");
  q.bindValue(":song_id", firstId);
  QVERIFY(q.exec());
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("New Title"));

  q.prepare("SELECT value_text FROM song_attributes WHERE song_id=:song_id AND "
            "key=:key");
  q.bindValue(":song_id", firstId);
  q.bindValue(":key", "rating");
  QVERIFY(q.exec());
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("5"));
}

void TestSongLibrary::loadFromDatabase_loadsBuiltInAndDynamicAttributes() {
  registry_->addOrUpdateDynamicColumn({.id = "attr:date_added",
                                       .title = "Date Added",
                                       .sortable = true,
                                       .visibleByDefault = false,
                                       .defaultWidth = 180},
                                      {.id = "attr:date_added",
                                       .source = ColumnSource::SongAttribute,
                                       .valueType = ValueType::DateTime,
                                       .displayKind = DisplayKind::Raw,
                                       .expression = "",
                                       .searchable = true,
                                       .writable = true});

  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  QVERIFY(
      q.exec("INSERT INTO songs(song_id, title, artist, album, discnumber, "
             "tracknumber, date, genre, filepath) "
             "VALUES (7, 'Song 7', 'Artist 7', 'Album 7', 1, 3, '2022-11-13', "
             "'Jazz', '/tmp/song-7.mp3')"));
  QVERIFY(q.exec(
      "INSERT INTO song_attributes(song_id, key, value_text, value_type) "
      "VALUES (7, 'date_added', '2022-11-13 06:45:23+00:00', 'date')"));

  library_->loadFromDatabase();

  const MSong &song = library_->getSongByPK(7);
  QCOMPARE(song.at("title").text, std::string("Song 7"));
  QVERIFY(song.contains("attr:date_added"));
  QCOMPARE(song.at("attr:date_added").text,
           std::string("2022-11-13 06:45:23+00:00"));
  const FieldDefinition *dateAddedDef =
      registry_->findField(QStringLiteral("attr:date_added"));
  QVERIFY(dateAddedDef != nullptr);
  QCOMPARE(dateAddedDef->valueType, ValueType::DateTime);
}

void TestSongLibrary::loadFromDatabase_assignsFieldIdsForAllLoadedFields() {
  registry_->addOrUpdateDynamicColumn({.id = "attr:rating",
                                       .title = "Rating",
                                       .sortable = true,
                                       .visibleByDefault = false,
                                       .defaultWidth = 120},
                                      {.id = "attr:rating",
                                       .source = ColumnSource::SongAttribute,
                                       .valueType = ValueType::Number,
                                       .displayKind = DisplayKind::Raw,
                                       .expression = "",
                                       .searchable = true,
                                       .writable = true});

  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  QVERIFY(q.exec(
      "INSERT INTO songs(song_id, title, artist, album, discnumber, "
      "tracknumber, date, genre, filepath) "
      "VALUES (71, 'Song 71', 'Artist 71', 'Album 71', 1, 7, '2022-11-13', "
      "'Jazz', '/tmp/song-71.mp3')"));
  QVERIFY(q.exec(
      "INSERT INTO song_attributes(song_id, key, value_text, value_type) "
      "VALUES (71, 'rating', '8', 'number')"));
  FakeProvider provider;
  registerFakeFields(*registry_);
  provider.rows.insert("song 71|artist 71", {3, 1234567890});
  library_->addProvidedSource(provider.fields());

  library_->loadFromDatabase();
  library_->fillProvidedValues("fake", provider.rows);

  const MSong &song = library_->getSongByPK(71);
  QVERIFY(song.contains("plays"));
  for (const auto &[key, value] : song) {
    QCOMPARE(value.fieldId, key);
  }
}

void TestSongLibrary::loadFromDatabase_removesUnknownDynamicAttributes() {
  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  QVERIFY(
      q.exec("INSERT INTO songs(song_id, title, artist, album, discnumber, "
             "tracknumber, date, genre, filepath) "
             "VALUES (8, 'Song 8', 'Artist 8', 'Album 8', 1, 4, '2022-11-14', "
             "'Rock', '/tmp/song-8.mp3')"));
  QVERIFY(q.exec(
      "INSERT INTO song_attributes(song_id, key, value_text, value_type) "
      "VALUES (8, 'orphan_tag', 'stale', 'text')"));

  library_->loadFromDatabase();

  const MSong &song = library_->getSongByPK(8);
  QVERIFY(!song.contains("attr:orphan_tag"));

  QVERIFY(
      q.exec("SELECT COUNT(*) FROM song_attributes WHERE key='orphan_tag'"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

void TestSongLibrary::loadFromDatabase_fillsProvidedValuesByKey() {
  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("INSERT INTO songs(song_id, title, artist, filepath) VALUES "
                 "(9, 'Song', 'Artist', '/tmp/song-9.mp3'), "
                 "(10, 'song ', 'ARTIST', '/tmp/song-10.mp3'), "
                 "(11, 'Other', 'Artist', '/tmp/song-11.mp3')"));
  FakeProvider provider;
  registerFakeFields(*registry_);
  // The plugin trims its keys; this one does not, so song 10 is its own.
  provider.rows.insert("song|artist", {7, 1710000000});
  provider.rows.insert("song |artist", {2, QVariant()});
  provider.rows.insert("gone|artist", {1, 1700000000});
  library_->addProvidedSource(provider.fields());

  library_->loadFromDatabase();
  library_->fillProvidedValues("fake", provider.rows);

  QCOMPARE(fieldText(*library_, 9, "plays"), std::string("7"));
  QCOMPARE(fieldText(*library_, 9, "played_at"), std::string("1710000000"));
  // An invalid value is no value.
  QCOMPARE(fieldText(*library_, 10, "plays"), std::string("2"));
  QCOMPARE(fieldText(*library_, 10, "played_at"), std::string("<none>"));
  QCOMPARE(fieldText(*library_, 11, "plays"), std::string("<none>"));
  // Typed by the source's types.
  QCOMPARE(library_->getSongByPK(9).at("plays").typed.numberDouble, 7.0);
  QVERIFY(provider.valuesCalls.isEmpty());
}

void TestSongLibrary::addToLibrary_refreshesProvidedValuesInOneBatch() {
  FakeProvider provider;
  registerFakeFields(*registry_);
  provider.rows.insert("a|x", {3, QVariant()});
  library_->addProvidedSource(provider.fields());
  int staleCalls = 0;
  library_->setStaleCallback([&staleCalls]() { ++staleCalls; });

  const int a1 =
      library_->addTolibrary(makeSong("A", "X", "/tmp/batch-a1.mp3"));
  const int a2 =
      library_->addTolibrary(makeSong("A", "X", "/tmp/batch-a2.mp3"));
  const int b = library_->addTolibrary(makeSong("B", "X", "/tmp/batch-b.mp3"));
  // One refresh for the batch, which reads the values: none yet.
  QCOMPARE(staleCalls, 1);
  QCOMPARE(fieldText(*library_, a1, "plays"), std::string("<none>"));

  std::vector<int> changed = library_->refreshStale();
  std::sort(changed.begin(), changed.end());
  QCOMPARE(changed, (std::vector<int>{a1, a2, b}));
  QCOMPARE(provider.valuesCalls, (QList<QStringList>{{"a|x", "b|x"}}));
  QCOMPARE(fieldText(*library_, a1, "plays"), std::string("3"));
  QCOMPARE(fieldText(*library_, a2, "plays"), std::string("3"));
  QCOMPARE(fieldText(*library_, b, "plays"), std::string("<none>"));

  // The next change starts another batch.
  library_->markStale("fake", {"a|x"});
  QCOMPARE(staleCalls, 2);
}

void TestSongLibrary::refreshStale_skipsRemovedSongsAndKeysNoSongHas() {
  FakeProvider provider;
  registerFakeFields(*registry_);
  library_->addProvidedSource(provider.fields());
  const int a = library_->addTolibrary(makeSong("A", "X", "/tmp/skip-a.mp3"));
  const int b = library_->addTolibrary(makeSong("B", "X", "/tmp/skip-b.mp3"));
  library_->refreshStale();
  provider.valuesCalls.clear();

  library_->removeSongs({b});
  library_->markStale("fake", {"b|x", "ghost|x", "a|x"});
  // A source that registered no fields.
  library_->markStale("other", {"a|x"});

  QCOMPARE(library_->refreshStale(), std::vector<int>{a});
  QCOMPARE(provider.valuesCalls, (QList<QStringList>{{"a|x"}}));
  // Nothing stale: no read.
  QVERIFY(library_->refreshStale().empty());
  QCOMPARE(provider.valuesCalls.size(), 1);
}

void TestSongLibrary::retag_dropsOldValuesAndReadsTheNewKey() {
  QString parsedTitle = "New";
  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [&parsedTitle](const std::string &path, const ColumnRegistry &,
                     std::unordered_map<std::string, std::string> *) {
        return makeSong(parsedTitle, "X", QString::fromStdString(path));
      });
  FakeProvider provider;
  registerFakeFields(*registry_);
  provider.rows.insert("old|x", {5, QVariant()});
  localLibrary.addProvidedSource(provider.fields());
  const std::string path = storedPath("/tmp/retag.mp3");
  const int pk = localLibrary.addTolibrary(
      makeSong("Old", "X", QString::fromStdString(path)));
  localLibrary.refreshStale();
  QCOMPARE(fieldText(localLibrary, pk, "plays"), std::string("5"));
  provider.valuesCalls.clear();

  // The old key's values go at once; the new key, which has none, is read at
  // the refresh. The old row is not carried over.
  localLibrary.refreshSongFromFile(path);
  QCOMPARE(fieldText(localLibrary, pk, "plays"), std::string("<none>"));
  QCOMPARE(localLibrary.refreshStale(), std::vector<int>{pk});
  QCOMPARE(provider.valuesCalls, (QList<QStringList>{{"new|x"}}));
  QCOMPARE(fieldText(localLibrary, pk, "plays"), std::string("<none>"));

  // Tagged back: the old key's values return.
  parsedTitle = "Old";
  localLibrary.refreshSongFromFile(path);
  localLibrary.refreshStale();
  QCOMPARE(fieldText(localLibrary, pk, "plays"), std::string("5"));

  // The same tags again: no key change, nothing to read.
  provider.valuesCalls.clear();
  localLibrary.refreshSongFromFile(path);
  QVERIFY(localLibrary.refreshStale().empty());
  QVERIFY(provider.valuesCalls.isEmpty());
}

void TestSongLibrary::load_keepsStaleKeysUntilReplaceWithSnapshot() {
  FakeProvider provider;
  registerFakeFields(*registry_);
  provider.rows.insert("a|x", {1, QVariant()});
  library_->addProvidedSource(provider.fields());
  const int pk = library_->addTolibrary(makeSong("A", "X", "/tmp/load-a.mp3"));
  library_->refreshStale();
  // The load's worker library, with the values as they were.
  SongLibrary worker(*registry_, *databaseManager_);
  worker.addProvidedSource(provider.fields());
  worker.loadFromDatabase();
  worker.fillProvidedValues("fake", provider.rows);

  // A change during the load is kept, not applied to the songs the snapshot
  // replaces.
  library_->beginLoad();
  provider.rows.insert("a|x", {2, QVariant()});
  library_->markStale("fake", {"a|x"});
  QVERIFY(library_->refreshStale().empty());

  library_->replaceWithSnapshot(worker.takeSnapshot());
  QCOMPARE(fieldText(*library_, pk, "plays"), std::string("2"));
}
void TestSongLibrary::search_matchesCaseInsensitiveExact() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-a.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-b.mp3",
                                  "Album", "2", "2024-01-01", "Jazz"));

  ExprParseResult parsed = library_->parseLibraryExpression("genre IS pop");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr, allPks());
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 1"));
}

void TestSongLibrary::search_supportsAndOr() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-c.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-d.mp3",
                                  "Album", "2", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 3", "Artist C", "/tmp/search-e.mp3",
                                  "Album", "3", "2024-01-01", "Jazz"));

  ExprParseResult parsed = library_->parseLibraryExpression(
      "artist IS artist c OR genre IS pop AND title IS song 2");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr, allPks());
  QCOMPARE(matches.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 2"));
  QCOMPARE(library_->getSongByPK(matches[1]).at("title").text,
           std::string("Song 3"));
}

void TestSongLibrary::search_supportsParentheses() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-pa.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-pb.mp3",
                                  "Album", "2", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 3", "Artist C", "/tmp/search-pc.mp3",
                                  "Album", "3", "2024-01-01", "Jazz"));

  ExprParseResult parsed = library_->parseLibraryExpression(
      "(artist IS artist c OR genre IS pop) AND title IS song 2");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr, allPks());
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsHasForTagLibStyleMultiValueFields() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-ha.mp3",
                                  "Album", "1", "2024-01-01",
                                  "Pop / Rock / Jazz"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-hb.mp3",
                                  "Album", "2", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 3", "Artist C", "/tmp/search-hc.mp3",
                                  "Album", "3", "2024-01-01", "Classical"));

  ExprParseResult parsed = library_->parseLibraryExpression("genre HAS rock");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr, allPks());
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 1"));

  ExprParseResult singleValueParsed =
      library_->parseLibraryExpression("genre HAS pop");
  QVERIFY(singleValueParsed.ok());

  const std::vector<int> singleValueMatches =
      library_->search(*singleValueParsed.expr, allPks());
  QCOMPARE(singleValueMatches.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(singleValueMatches[0]).at("title").text,
           std::string("Song 1"));
  QCOMPARE(library_->getSongByPK(singleValueMatches[1]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsInList() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-ia.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-ib.mp3",
                                  "Album", "2", "2024-01-01", "Rock"));
  library_->addTolibrary(makeSong("Song 3", "Artist C", "/tmp/search-ic.mp3",
                                  "Album", "3", "2024-01-01", "Classical"));

  ExprParseResult parsed =
      library_->parseLibraryExpression("genre IN [pop, rock]");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr, allPks());
  QCOMPARE(matches.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 1"));
  QCOMPARE(library_->getSongByPK(matches[1]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsInRange() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-ir-a.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-ir-b.mp3",
                                  "Album", "3", "2024-06-01", "Rock"));
  library_->addTolibrary(makeSong("Song 3", "Artist C", "/tmp/search-ir-c.mp3",
                                  "Album", "7", "2026-01-01", "Jazz"));

  ExprParseResult numberParsed =
      library_->parseLibraryExpression("tracknumber IN [2..5]");
  QVERIFY(numberParsed.ok());
  const std::vector<int> numberMatches =
      library_->search(*numberParsed.expr, allPks());
  QCOMPARE(numberMatches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(numberMatches[0]).at("title").text,
           std::string("Song 2"));

  ExprParseResult dateParsed =
      library_->parseLibraryExpression("date IN [2024-01-01..2024-12-31]");
  QVERIFY(dateParsed.ok());
  const std::vector<int> dateMatches =
      library_->search(*dateParsed.expr, allPks());
  QCOMPARE(dateMatches.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(dateMatches[0]).at("title").text,
           std::string("Song 1"));
  QCOMPARE(library_->getSongByPK(dateMatches[1]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsQuotedValues() {
  library_->addTolibrary(makeSong("rock, pop suite", "Artist A",
                                  "/tmp/search-qv-a.mp3", "Album", "1",
                                  "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-qv-b.mp3",
                                  "Album", "2", "2024-01-01", "rock, pop"));

  ExprParseResult scalarParsed =
      library_->parseLibraryExpression("title IS \"rock, pop suite\"");
  QVERIFY(scalarParsed.ok());
  const std::vector<int> scalarMatches =
      library_->search(*scalarParsed.expr, allPks());
  QCOMPARE(scalarMatches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(scalarMatches[0]).at("title").text,
           std::string("rock, pop suite"));

  ExprParseResult listParsed =
      library_->parseLibraryExpression("genre IN [\"rock, pop\", jazz]");
  QVERIFY(listParsed.ok());
  const std::vector<int> listMatches =
      library_->search(*listParsed.expr, allPks());
  QCOMPARE(listMatches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(listMatches[0]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsNumericFieldComparison() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-num-a.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-num-b.mp3",
                                  "Album", "2", "2026", "Rock"));

  ExprParseResult parsed = library_->parseLibraryExpression("tracknumber = 2");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr, allPks());
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 2"));

  ExprParseResult typedNumberParsed =
      library_->parseLibraryExpression("tracknumber = 2.0");
  QVERIFY(typedNumberParsed.ok());
  const std::vector<int> typedNumberMatches =
      library_->search(*typedNumberParsed.expr, allPks());
  QCOMPARE(typedNumberMatches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(typedNumberMatches[0]).at("title").text,
           std::string("Song 2"));

  ExprParseResult typedDateParsed =
      library_->parseLibraryExpression("date IS 2026-01-01");
  QVERIFY(typedDateParsed.ok());
  const std::vector<int> typedDateMatches =
      library_->search(*typedDateParsed.expr, allPks());
  QCOMPARE(typedDateMatches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(typedDateMatches[0]).at("title").text,
           std::string("Song 2"));

  ExprParseResult typedHasNumberParsed =
      library_->parseLibraryExpression("tracknumber HAS 2.0");
  QVERIFY(typedHasNumberParsed.ok());
  const std::vector<int> typedHasNumberMatches =
      library_->search(*typedHasNumberParsed.expr, allPks());
  QCOMPARE(typedHasNumberMatches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(typedHasNumberMatches[0]).at("title").text,
           std::string("Song 2"));

  ExprParseResult typedHasDateParsed =
      library_->parseLibraryExpression("date HAS 2026-01-01");
  QVERIFY(typedHasDateParsed.ok());
  const std::vector<int> typedHasDateMatches =
      library_->search(*typedHasDateParsed.expr, allPks());
  QCOMPARE(typedHasDateMatches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(typedHasDateMatches[0]).at("title").text,
           std::string("Song 2"));

  ExprParseResult invalidParsed =
      library_->parseLibraryExpression("tracknumber = two");
  QVERIFY(!invalidParsed.ok());
}

void TestSongLibrary::search_supportsRelationalComparisons() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-rel-a.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-rel-b.mp3",
                                  "Album", "2", "2024-01-02", "Rock"));
  library_->addTolibrary(makeSong("Song 3", "Artist C", "/tmp/search-rel-c.mp3",
                                  "Album", "3", "2024-01-03", "Jazz"));

  ExprParseResult ltParsed =
      library_->parseLibraryExpression("tracknumber < 3");
  QVERIFY(ltParsed.ok());
  const std::vector<int> ltMatches = library_->search(*ltParsed.expr, allPks());
  QCOMPARE(ltMatches.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(ltMatches[0]).at("title").text,
           std::string("Song 1"));
  QCOMPARE(library_->getSongByPK(ltMatches[1]).at("title").text,
           std::string("Song 2"));

  ExprParseResult gteParsed =
      library_->parseLibraryExpression("tracknumber >= 2");
  QVERIFY(gteParsed.ok());
  const std::vector<int> gteMatches =
      library_->search(*gteParsed.expr, allPks());
  QCOMPARE(gteMatches.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(gteMatches[0]).at("title").text,
           std::string("Song 2"));
  QCOMPARE(library_->getSongByPK(gteMatches[1]).at("title").text,
           std::string("Song 3"));

  ExprParseResult dateParsed =
      library_->parseLibraryExpression("date >= 2024-01-02");
  QVERIFY(dateParsed.ok());
  const std::vector<int> dateMatches =
      library_->search(*dateParsed.expr, allPks());
  QCOMPARE(dateMatches.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(dateMatches[0]).at("title").text,
           std::string("Song 2"));
  QCOMPARE(library_->getSongByPK(dateMatches[1]).at("title").text,
           std::string("Song 3"));

  ExprParseResult invalidParsed =
      library_->parseLibraryExpression("tracknumber < two");
  QVERIFY(!invalidParsed.ok());
}

void TestSongLibrary::search_relationalComparisonSkipsMissingDate() {
  library_->addTolibrary(makeSong(
      "Song 1", "Artist A", "/tmp/search-miss-a.mp3", "Album", "1", "", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B",
                                  "/tmp/search-miss-b.mp3", "Album", "2",
                                  "1999-01-01", "Rock"));

  ExprParseResult parsed = library_->parseLibraryExpression("date < 2000");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr, allPks());
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_providedDateTimeField_supportsEquality() {
  FakeProvider provider;
  registerFakeFields(*registry_);
  const qint64 expectedEpochSeconds =
      QDateTime(QDate(2026, 5, 13), QTime(1, 45, 59)).toSecsSinceEpoch();
  provider.rows.insert("song 1|artist a", {1, expectedEpochSeconds});
  library_->addProvidedSource(provider.fields());
  const int songPk = library_->addTolibrary(
      makeSong("Song 1", "Artist A", "/tmp/search-last-played.mp3"));
  library_->addTolibrary(
      makeSong("Song 2", "Artist A", "/tmp/search-never-played.mp3"));
  library_->refreshStale();

  ExprParseResult parsed =
      library_->parseLibraryExpression("played_at = 2026-05-13 01:45:59");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr, allPks());
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(matches[0], songPk);
}

void TestSongLibrary::search_supportsNot() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-na.mp3",
                                  "Album", "1", "2024-01-01",
                                  "Pop / Rock / Jazz"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-nb.mp3",
                                  "Album", "2", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 3", "Artist C", "/tmp/search-nc.mp3",
                                  "Album", "3", "2024-01-01", "Classical"));

  ExprParseResult parsed =
      library_->parseLibraryExpression("NOT genre HAS rock AND genre HAS pop");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr, allPks());
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsCustomFields() {
  registry_->addOrUpdateDynamicColumn({.id = "attr:musicbrainz_trackid",
                                       .title = "MusicBrainz Track ID",
                                       .sortable = true,
                                       .visibleByDefault = false,
                                       .defaultWidth = 140},
                                      {.id = "attr:musicbrainz_trackid",
                                       .source = ColumnSource::SongAttribute,
                                       .valueType = ValueType::Text,
                                       .displayKind = DisplayKind::Raw,
                                       .expression = "",
                                       .searchable = true,
                                       .writable = true});

  MSong first = makeSong("Song 1", "Artist A", "/tmp/search-f.mp3");
  first.insert_or_assign("attr:musicbrainz_trackid",
                         FieldValue("abc123", "attr:musicbrainz_trackid"));
  library_->addTolibrary(std::move(first));

  MSong second = makeSong("Song 2", "Artist B", "/tmp/search-g.mp3");
  second.insert_or_assign("attr:musicbrainz_trackid",
                          FieldValue("def456", "attr:musicbrainz_trackid"));
  library_->addTolibrary(std::move(second));

  ExprParseResult parsed =
      library_->parseLibraryExpression("musicbrainz_trackid IS DEF456");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr, allPks());
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsComputedFields() {
  QVERIFY(registry_->upsertComputedDefinition(
      databaseManager_->db(),
      {.id = "computed:era",
       .title = "Era",
       .sortable = true,
       .visibleByDefault = true,
       .defaultWidth = 140},
      {.id = "computed:era",
       .source = ColumnSource::Computed,
       .valueType = ValueType::Text,
       .displayKind = DisplayKind::Raw,
       .expression = "IF date < 2000 THEN classic ELSE new",
       .searchable = true,
       .writable = false}));
  QVERIFY(registry_->loadDynamicColumns(databaseManager_->db()));

  SongLibrary localLibrary(*registry_, *databaseManager_);
  MSong first = makeSong("Song 1", "Artist A", "/tmp/search-comp-a.mp3",
                         "Album", "1", "1998-01-01", "Pop");
  first.insert_or_assign("computed:era", FieldValue("classic", "computed:era"));
  localLibrary.addTolibrary(std::move(first));
  MSong second = makeSong("Song 2", "Artist B", "/tmp/search-comp-b.mp3",
                          "Album", "2", "2021-01-01", "Rock");
  second.insert_or_assign("computed:era", FieldValue("new", "computed:era"));
  localLibrary.addTolibrary(std::move(second));

  ExprParseResult parsed =
      localLibrary.parseLibraryExpression("era IS classic");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = localLibrary.search(*parsed.expr, allPks());
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(localLibrary.getSongByPK(matches[0]).at("title").text,
           std::string("Song 1"));

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("SELECT value_text FROM song_computed_attributes WHERE "
                 "key='era' ORDER BY song_id"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("classic"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("new"));
}

void TestSongLibrary::search_noMatchReturnsEmpty() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-h.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));

  ExprParseResult parsed =
      library_->parseLibraryExpression("artist IS artist z");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr, allPks());
  QVERIFY(matches.empty());
}

void TestSongLibrary::search_onlyCandidatesInFilepathOrder() {
  const int c = library_->addTolibrary(
      makeSong("Song C", "Artist", "/tmp/search-order-c.mp3", "Album", "1"));
  const int a = library_->addTolibrary(
      makeSong("Song A", "Artist", "/tmp/search-order-a.mp3", "Album", "2"));
  const int b = library_->addTolibrary(
      makeSong("Song B", "Artist", "/tmp/search-order-b.mp3", "Album", "3"));

  ExprParseResult parsed = library_->parseLibraryExpression("true");
  QVERIFY(parsed.ok());

  QCOMPARE(library_->search(*parsed.expr, {c, b, a}),
           (std::vector<int>{a, b, c}));
  QCOMPARE(library_->search(*parsed.expr, {c, a}), (std::vector<int>{a, c}));
}

void TestSongLibrary::refreshSongFromFile_usesInjectedParserAndSyncsDb() {
  registry_->addOrUpdateDynamicColumn({.id = "attr:rating",
                                       .title = "Rating",
                                       .sortable = true,
                                       .visibleByDefault = false,
                                       .defaultWidth = 120},
                                      {.id = "attr:rating",
                                       .source = ColumnSource::SongAttribute,
                                       .valueType = ValueType::Number,
                                       .displayKind = DisplayKind::Raw,
                                       .expression = "",
                                       .searchable = true,
                                       .writable = true});

  int parseCallCount = 0;
  std::string parsedPath;
  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [&](const std::string &path, const ColumnRegistry &,
          std::unordered_map<std::string, std::string> *) -> MSong {
        ++parseCallCount;
        parsedPath = path;
        MSong parsed =
            makeSong("Refreshed", "Artist B", QString::fromStdString(path),
                     "Album B", "9", "2024-02-03", "Pop");
        parsed.insert_or_assign("attr:rating", FieldValue("8", "attr:rating"));
        return parsed;
      });

  const std::string path = storedPath("/tmp/refresh-single.mp3");
  const int songId = localLibrary.addTolibrary(
      makeSong("Old", "Artist A", QString::fromStdString(path)));

  const MSong &beforeRefresh = localLibrary.getSongByPK(songId);
  QCOMPARE(beforeRefresh.at("title").text, std::string("Old"));
  QCOMPARE(beforeRefresh.at("artist").text, std::string("Artist A"));

  const MSong &refreshed = localLibrary.refreshSongFromFile(path);

  QCOMPARE(parseCallCount, 1);
  QCOMPARE(parsedPath, path);
  QCOMPARE(refreshed.at("title").text, std::string("Refreshed"));
  QCOMPARE(refreshed.at("artist").text, std::string("Artist B"));
  QCOMPARE(refreshed.at("album").text, std::string("Album B"));
  QVERIFY(refreshed.contains("attr:rating"));
  QCOMPARE(refreshed.at("attr:rating").text, std::string("8"));

  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  q.prepare("SELECT title, artist, album FROM songs WHERE song_id=:song_id");
  q.bindValue(":song_id", songId);
  QVERIFY(q.exec());
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("Refreshed"));
  QCOMPARE(q.value(1).toString(), QString("Artist B"));
  QCOMPARE(q.value(2).toString(), QString("Album B"));

  q.prepare("SELECT value_text FROM song_attributes WHERE song_id=:song_id AND "
            "key='rating'");
  q.bindValue(":song_id", songId);
  QVERIFY(q.exec());
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("8"));
}

void TestSongLibrary::refreshSongsFromFilepaths_dedupesAndReportsProgress() {
  int parseCallCount = 0;
  std::vector<std::string> parsedPaths;
  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [&](const std::string &path, const ColumnRegistry &,
          std::unordered_map<std::string, std::string> *) -> MSong {
        ++parseCallCount;
        parsedPaths.push_back(path);
        return makeSong(QString::fromStdString("Refreshed:" + path), "Artist",
                        QString::fromStdString(path), "Album", "1", "2024",
                        "Genre");
      });

  const std::string pathA = storedPath("/tmp/refresh-list-a.mp3");
  const std::string pathB = storedPath("/tmp/refresh-list-b.mp3");
  const int idA = localLibrary.addTolibrary(
      makeSong("A", "Artist", QString::fromStdString(pathA)));
  const int idB = localLibrary.addTolibrary(
      makeSong("B", "Artist", QString::fromStdString(pathB)));

  std::vector<std::pair<int, int>> progress;
  localLibrary.refreshSongsFromFilepaths(
      {pathA, pathA, pathB},
      [&](int current, int total) { progress.emplace_back(current, total); });

  QCOMPARE(parseCallCount, 2);
  const std::vector<std::string> expectedPaths = {pathA, pathB};
  QCOMPARE(parsedPaths, expectedPaths);
  const std::vector<std::pair<int, int>> expectedProgress = {{1, 2}, {2, 2}};
  QCOMPARE(progress, expectedProgress);

  QCOMPARE(localLibrary.getSongByPK(idA).at("title").text,
           "Refreshed:" + pathA);
  QCOMPARE(localLibrary.getSongByPK(idB).at("title").text,
           "Refreshed:" + pathB);
}

void TestSongLibrary::refreshSongFromFile_populatesRemainingFields() {
  bool sawNullRemaining = false;
  bool sawProvidedRemaining = false;
  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [&](const std::string &path, const ColumnRegistry &,
          std::unordered_map<std::string, std::string> *remainingFields)
          -> MSong {
        if (remainingFields) {
          sawProvidedRemaining = true;
          (*remainingFields)["unsynced_lyrics"] = "[00:01.00]embedded";
        } else {
          sawNullRemaining = true;
        }
        return makeSong("Loaded", "Artist", QString::fromStdString(path));
      });

  localLibrary.loadSongFromFile("/tmp/embedded-off.mp3");
  QVERIFY(sawNullRemaining);
  QVERIFY(!sawProvidedRemaining);

  const std::string path = storedPath("/tmp/embedded-on.mp3");
  localLibrary.addTolibrary(
      makeSong("Old", "Artist", QString::fromStdString(path)));
  std::unordered_map<std::string, std::string> remainingFields;
  localLibrary.refreshSongFromFile(path, &remainingFields);
  QVERIFY(sawProvidedRemaining);
  QCOMPARE(remainingFields.at("unsynced_lyrics"),
           std::string("[00:01.00]embedded"));
}

void TestSongLibrary::refreshSongFromFile_unparsableFileKeepsStoredFields() {
  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [](const std::string &, const ColumnRegistry &,
         std::unordered_map<std::string, std::string> *)
          -> std::optional<MSong> { return std::nullopt; });

  const std::string path = storedPath("/tmp/refresh-unparsable.mp3");
  const int songId = localLibrary.addTolibrary(
      makeSong("Stored", "Artist A", QString::fromStdString(path), "Album A"));

  const MSong &refreshed = localLibrary.refreshSongFromFile(path);

  QCOMPARE(refreshed.at("title").text, std::string("Stored"));
  QCOMPARE(refreshed.at("artist").text, std::string("Artist A"));
  QCOMPARE(refreshed.at("album").text, std::string("Album A"));

  QSqlQuery q(databaseManager_->db());
  q.prepare("SELECT title, artist, album FROM songs WHERE song_id=:song_id");
  q.bindValue(":song_id", songId);
  QVERIFY(q.exec());
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("Stored"));
  QCOMPARE(q.value(1).toString(), QString("Artist A"));
  QCOMPARE(q.value(2).toString(), QString("Album A"));
}

void TestSongLibrary::refreshSongFromFile_customLyricsFieldsPersist() {
  registry_->addOrUpdateDynamicColumn({.id = "attr:unsynced_lyrics",
                                       .title = "Unsynced Lyrics",
                                       .sortable = true,
                                       .visibleByDefault = false,
                                       .defaultWidth = 200},
                                      {.id = "attr:unsynced_lyrics",
                                       .source = ColumnSource::SongAttribute,
                                       .valueType = ValueType::Text,
                                       .displayKind = DisplayKind::Raw,
                                       .expression = "",
                                       .searchable = true,
                                       .writable = true});
  registry_->addOrUpdateDynamicColumn({.id = "attr:lyrics",
                                       .title = "Lyrics",
                                       .sortable = true,
                                       .visibleByDefault = false,
                                       .defaultWidth = 200},
                                      {.id = "attr:lyrics",
                                       .source = ColumnSource::SongAttribute,
                                       .valueType = ValueType::Text,
                                       .displayKind = DisplayKind::Raw,
                                       .expression = "",
                                       .searchable = true,
                                       .writable = true});

  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [&](const std::string &path, const ColumnRegistry &,
          std::unordered_map<std::string, std::string> *remainingFields)
          -> MSong {
        if (remainingFields) {
          (*remainingFields)["unsynced_lyrics"] = "[00:00.50]from-tag";
        }
        MSong parsed =
            makeSong("Refreshed", "Artist", QString::fromStdString(path));
        parsed.insert_or_assign("attr:unsynced_lyrics",
                                FieldValue("big text", "attr:unsynced_lyrics"));
        parsed.insert_or_assign("attr:lyrics",
                                FieldValue("fallback text", "attr:lyrics"));
        return parsed;
      });

  const std::string path = storedPath("/tmp/embedded-refresh.mp3");
  localLibrary.addTolibrary(
      makeSong("Old", "Artist", QString::fromStdString(path)));

  std::unordered_map<std::string, std::string> remainingFields;
  const MSong &refreshed =
      localLibrary.refreshSongFromFile(path, &remainingFields);
  QCOMPARE(remainingFields.at("unsynced_lyrics"),
           std::string("[00:00.50]from-tag"));
  QVERIFY(refreshed.contains("attr:unsynced_lyrics"));
  QVERIFY(refreshed.contains("attr:lyrics"));
  QCOMPARE(refreshed.at("attr:unsynced_lyrics").text, std::string("big text"));
  QCOMPARE(refreshed.at("attr:lyrics").text, std::string("fallback text"));

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("SELECT COUNT(*) FROM song_attributes WHERE key IN "
                 "('unsynced_lyrics', 'lyrics')"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 2);
}

void TestSongLibrary::registerQuery_tracksLaterInsertions() {
  const std::vector<int> &artistA = library_->registerQuery("ArtistA");
  QCOMPARE(artistA.size(), static_cast<size_t>(0));

  const int id1 =
      library_->addTolibrary(makeSong("S1", "ArtistA", "/tmp/a1.mp3"));
  library_->addTolibrary(makeSong("S2", "ArtistB", "/tmp/b1.mp3"));
  const int id3 =
      library_->addTolibrary(makeSong("S3", "ArtistA", "/tmp/a2.mp3"));

  const std::vector<int> expected = {id1, id3};
  QCOMPARE(artistA, expected);
}

void TestSongLibrary::queryHelpers_skipEmptySongSlots() {
  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  QVERIFY(
      q.exec("INSERT INTO songs(song_id, title, artist, album, discnumber, "
             "tracknumber, date, genre, filepath) "
             "VALUES (5, 'S5', 'ArtistX', 'Album', 1, 1, '2024-01-01', 'Pop', "
             "'/tmp/s5.mp3')"));
  QVERIFY(
      q.exec("INSERT INTO songs(song_id, title, artist, album, discnumber, "
             "tracknumber, date, genre, filepath) "
             "VALUES (9, 'S9', 'ArtistY', 'Album', 1, 2, '2024-01-01', 'Pop', "
             "'/tmp/s9.mp3')"));

  library_->loadFromDatabase();

  const std::vector<int> matches = library_->query("ArtistX");
  QCOMPARE(matches.size(), static_cast<size_t>(1));
  QCOMPARE(matches[0], 5);

  const std::unordered_set<std::string> artists =
      library_->queryField("artist");
  QCOMPARE(artists.size(), static_cast<size_t>(2));
  QVERIFY(artists.find("ArtistX") != artists.end());
  QVERIFY(artists.find("ArtistY") != artists.end());
}

void TestSongLibrary::removeSongs_deletesSongAndAttributes() {
  registry_->addOrUpdateDynamicColumn({.id = "attr:mood", .title = "Mood"},
                                      {.id = "attr:mood",
                                       .source = ColumnSource::SongAttribute,
                                       .valueType = ValueType::Text,
                                       .searchable = true,
                                       .writable = true});
  MSong song = makeSong("Song", "Artist", "/tmp/remove-song.mp3");
  song.insert_or_assign("attr:mood", FieldValue("happy", "attr:mood"));
  const int removedId = library_->addTolibrary(std::move(song));
  const int keptId = library_->addTolibrary(
      makeSong("Kept", "Artist", "/tmp/remove-kept.mp3"));
  QSqlDatabase &db = databaseManager_->db();
  const auto count = [&db](const QString &sql) {
    QSqlQuery q(db);
    return q.exec(sql) && q.next() ? q.value(0).toInt() : -1;
  };
  const QString attributesOfRemoved =
      QString("SELECT COUNT(*) FROM song_attributes WHERE song_id=%1")
          .arg(removedId);
  QCOMPARE(count(attributesOfRemoved), 1);

  library_->removeSongs({removedId});

  QVERIFY(library_->getSongByPK(removedId).empty());
  QCOMPARE(library_->getSongByPK(keptId).at("title").text, std::string("Kept"));
  QCOMPARE(count(QString("SELECT COUNT(*) FROM songs WHERE song_id=%1")
                     .arg(removedId)),
           0);
  QCOMPARE(count(attributesOfRemoved), 0);

  // The same file comes back as a new song.
  const int readdedId = library_->addTolibrary(
      makeSong("Song", "Artist", "/tmp/remove-song.mp3"));
  QVERIFY(readdedId != removedId);
}

void TestSongLibrary::removeSongs_updatesRegisteredQueries() {
  const std::vector<int> &artistA = library_->registerQuery("ArtistA");
  const std::unordered_set<std::string> &artists =
      library_->registerQueryField("artist");
  const int a1 =
      library_->addTolibrary(makeSong("S1", "ArtistA", "/tmp/rq-a1.mp3"));
  const int a2 =
      library_->addTolibrary(makeSong("S2", "ArtistA", "/tmp/rq-a2.mp3"));
  const int b =
      library_->addTolibrary(makeSong("S3", "ArtistB", "/tmp/rq-b.mp3"));

  library_->removeSongs({a1, b});

  QCOMPARE(artistA, std::vector<int>{a2});
  QCOMPARE(artists, std::unordered_set<std::string>{"ArtistA"});
}

QTEST_MAIN(TestSongLibrary)
#include "tst_songlibrary.moc"
