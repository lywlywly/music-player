#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../monitoredlibrary.h"
#include "../playlistcollection.h"
#include "../songlibrary.h"

namespace {
// Test files hold "<title>;<genre>".
std::optional<MSong> fakeParse(const std::string &path) {
  QFile file(QString::fromStdString(path));
  if (!file.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }
  const QList<QByteArray> parts = file.readAll().trimmed().split(';');
  MSong song;
  song.insert_or_assign("title",
                        FieldValue(parts.value(0).toStdString(), "title"));
  song.insert_or_assign("genre",
                        FieldValue(parts.value(1).toStdString(), "genre"));
  song.insert_or_assign("artist", FieldValue("Artist", "artist"));
  song.insert_or_assign("album", FieldValue("Album", "album"));
  song.insert_or_assign("filepath", FieldValue(path, "filepath"));
  return song;
}

bool writeFile(const QString &path, const QByteArray &content) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return file.write(content) == content.size();
}

int queryInt(QSqlDatabase &db, const QString &sql) {
  QSqlQuery q(db);
  if (!q.exec(sql) || !q.next()) {
    return -1;
  }
  return q.value(0).toInt();
}

std::vector<int> rows(const Playlist &playlist) {
  std::vector<int> pks;
  for (int row = 0; row < playlist.songCount(); ++row) {
    pks.push_back(playlist.getPkByIndex(row));
  }
  return pks;
}
} // namespace

class TestAutoPlaylist : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void parseSongFilter_acceptsOnlyConditions();
  void playlist_autoKindRefusesWritesAndStoresNoItems();
  void create_fillsMatchingMonitoredSongsInFilepathOrder();
  void loadAll_restoresAutoPlaylists();
  void scanChanges_updateRowsBeforeRelease();
  void notifySongChanged_reevaluatesPlayStats();
  void refreshPlaylistMetadata_updatesAutoPlaylists();
  void invalidExpression_hasNoRowsAndKeepsText();
  void reloadAutoPlaylistExpressions_resolvesNewFields();
  void removeAutoPlaylist_releasesNothing();

private:
  void rescanAndWait();
  // Watches root() and waits for the scan.
  void watchRoot();
  int pkOf(const QString &name) const;
  QString root() const;

  QTemporaryDir *dir_ = nullptr;
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  MonitoredLibrary *monitored_ = nullptr;
  PlaylistCollection *collection_ = nullptr;
  QString connectionName_;
};

void TestAutoPlaylist::init() {
  qRegisterMetaType<std::vector<int>>();
  dir_ = new QTemporaryDir();
  QVERIFY(dir_->isValid());
  QVERIFY(QDir().mkpath(root()));
  connectionName_ =
      QStringLiteral("test_autoplaylist_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ =
      new SongLibrary(*registry_, *databaseManager_,
                      [](const std::string &path, const ColumnRegistry &,
                         std::unordered_map<std::string, std::string> *) {
                        return fakeParse(path);
                      });
  monitored_ = new MonitoredLibrary(
      *library_, *registry_, *databaseManager_,
      [](const std::string &path, const ColumnRegistry &) {
        return fakeParse(path);
      });
  monitored_->load();
  collection_ =
      new PlaylistCollection(*library_, *databaseManager_, *monitored_);
  collection_->loadAll();
}

void TestAutoPlaylist::cleanup() {
  QTRY_VERIFY_WITH_TIMEOUT(!monitored_->isScanning(), 5000);
  delete collection_;
  collection_ = nullptr;
  delete monitored_;
  monitored_ = nullptr;
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
  delete dir_;
  dir_ = nullptr;
}

QString TestAutoPlaylist::root() const {
  return QFileInfo(dir_->path()).canonicalFilePath() + "/music";
}

void TestAutoPlaylist::rescanAndWait() {
  monitored_->rescan();
  QTRY_VERIFY_WITH_TIMEOUT(!monitored_->isScanning(), 5000);
}

void TestAutoPlaylist::watchRoot() {
  const MonitoredLibrary::AddFolderResult added = monitored_->addFolder(root());
  QVERIFY2(added.folderId > 0, qPrintable(added.error));
  QTRY_VERIFY_WITH_TIMEOUT(!monitored_->isScanning(), 5000);
}

int TestAutoPlaylist::pkOf(const QString &name) const {
  for (int pk : monitored_->songPks()) {
    if (library_->getSongByPK(pk).at("filepath").text ==
        (root() + "/" + name).toStdString()) {
      return pk;
    }
  }
  return -1;
}

void TestAutoPlaylist::parseSongFilter_acceptsOnlyConditions() {
  for (const QString &condition :
       {QStringLiteral("true"), QStringLiteral("genre HAS jazz")}) {
    const ExprParseResult result = parseSongFilter(*library_, condition);
    QVERIFY2(result.ok(), qPrintable(condition));
  }
  for (const QString &value :
       {QStringLiteral("davis"), QStringLiteral("1"), QStringLiteral("title")}) {
    const ExprParseResult result = parseSongFilter(*library_, value);
    QVERIFY2(!result.ok(), qPrintable(value));
    QCOMPARE(result.error.message, QString("Expression must be a condition"));
  }
  const ExprParseResult syntaxError =
      parseSongFilter(*library_, QStringLiteral("genre IN [pop"));
  QVERIFY(!syntaxError.ok());
  QCOMPARE(syntaxError.error.message,
           library_->parseLibraryExpression("genre IN [pop").error.message);
}

void TestAutoPlaylist::playlist_autoKindRefusesWritesAndStoresNoItems() {
  QVERIFY(writeFile(root() + "/a.mp3", "A;jazz"));
  QVERIFY(writeFile(root() + "/b.mp3", "B;jazz"));
  watchRoot();
  const int a = pkOf("a.mp3");
  const int b = pkOf("b.mp3");
  const int id = collection_->createAutoPlaylist("Jazz", "false");
  Playlist &playlist = *collection_->playlist(id);
  QCOMPARE(playlist.kind(), Playlist::Kind::Auto);

  QSignalSpy removedPksSpy(&playlist, &Playlist::songPksRemoved);
  QSignalSpy insertedSpy(&playlist, &Playlist::songsInserted);
  QSignalSpy removedSpy(&playlist, &Playlist::songsRemoved);
  QSignalSpy resetSpy(&playlist, &Playlist::songsReset);
  playlist.resetRows({b});
  playlist.appendRows({b, a});
  QCOMPARE(rows(playlist), (std::vector<int>{b, a}));
  playlist.removeRowsByPk({b});
  QCOMPARE(rows(playlist), std::vector<int>{a});
  QCOMPARE(resetSpy.count(), 1);
  QCOMPARE(insertedSpy.count(), 1);
  QCOMPARE(removedSpy.count(), 1);

  QVERIFY(playlist.addFiles({(root() + "/b.mp3").toStdString()}).empty());
  QVERIFY(!playlist.addSongByPk(b));
  playlist.removeSongs({0});
  playlist.clear();
  QCOMPARE(rows(playlist), std::vector<int>{a});
  QCOMPARE(removedPksSpy.count(), 0);
  QCOMPARE(queryInt(databaseManager_->db(),
                    "SELECT COUNT(*) FROM playlist_items"),
           0);

  // loadState reads only last_played.
  playlist.setLastPlayed(a);
  Playlist reloaded(id, *library_, *databaseManager_, Playlist::Kind::Auto);
  QVERIFY(reloaded.loadState());
  QCOMPARE(reloaded.getLastPlayed(), a);
  QVERIFY(reloaded.empty());
}

void TestAutoPlaylist::create_fillsMatchingMonitoredSongsInFilepathOrder() {
  QVERIFY(writeFile(root() + "/b.mp3", "B;jazz"));
  QVERIFY(writeFile(root() + "/a.mp3", "A;jazz"));
  QVERIFY(writeFile(root() + "/c.mp3", "C;rock"));
  watchRoot();
  // A manual-only song never appears.
  const QString manualPath = dir_->path() + "/manual.mp3";
  QVERIFY(writeFile(manualPath, "M;jazz"));
  QCOMPARE(collection_->playlist(1)
               ->addFiles({manualPath.toStdString()})
               .size(),
           size_t{1});
  QSignalSpy createdSpy(collection_, &PlaylistCollection::playlistCreated);

  const int id = collection_->createAutoPlaylist("Jazz", "genre HAS jazz");

  QCOMPARE(createdSpy.count(), 1);
  QVERIFY(collection_->isAuto(id));
  QVERIFY(!collection_->isAuto(1));
  QCOMPARE(collection_->autoPlaylistExpression(id), QString("genre HAS jazz"));
  QVERIFY(collection_->autoPlaylistError(id).isEmpty());
  QCOMPARE(rows(*collection_->playlist(id)),
           (std::vector<int>{pkOf("a.mp3"), pkOf("b.mp3")}));
  QCOMPARE(queryInt(databaseManager_->db(),
                    QString("SELECT COUNT(*) FROM playlists WHERE "
                            "playlist_id=%1 AND kind='auto' AND "
                            "expression='genre HAS jazz'")
                        .arg(id)),
           1);
}

void TestAutoPlaylist::loadAll_restoresAutoPlaylists() {
  QVERIFY(writeFile(root() + "/b.mp3", "B;jazz"));
  QVERIFY(writeFile(root() + "/a.mp3", "A;jazz"));
  watchRoot();
  const int valid = collection_->createAutoPlaylist("Jazz", "genre HAS jazz");
  const int invalid = collection_->createAutoPlaylist("Bad", "davis");

  PlaylistCollection reloaded(*library_, *databaseManager_, *monitored_);
  reloaded.loadAll();

  QCOMPARE(reloaded.playlistIdsInOrder(),
           (std::vector<int>{1, valid, invalid}));
  QCOMPARE(reloaded.playlist(valid)->kind(), Playlist::Kind::Auto);
  QCOMPARE(reloaded.playlist(1)->kind(), Playlist::Kind::Manual);
  QCOMPARE(rows(*reloaded.playlist(valid)),
           (std::vector<int>{pkOf("a.mp3"), pkOf("b.mp3")}));
  QVERIFY(reloaded.playlist(invalid)->empty());
  QCOMPARE(reloaded.autoPlaylistExpression(invalid), QString("davis"));
  QVERIFY(!reloaded.autoPlaylistError(invalid).isEmpty());
}

void TestAutoPlaylist::scanChanges_updateRowsBeforeRelease() {
  QVERIFY(writeFile(root() + "/b.mp3", "B;jazz"));
  watchRoot();
  const int id = collection_->createAutoPlaylist("Jazz", "genre HAS jazz");
  Playlist &playlist = *collection_->playlist(id);
  const int b = pkOf("b.mp3");

  // A song that starts matching is appended, even before b in filepath order.
  QVERIFY(writeFile(root() + "/a.mp3", "A;jazz"));
  rescanAndWait();
  const int a = pkOf("a.mp3");
  QCOMPARE(rows(playlist), (std::vector<int>{b, a}));

  // A changed song that still matches keeps its row.
  QSignalSpy dataChangedSpy(&playlist, &Playlist::songDataChanged);
  QVERIFY(writeFile(root() + "/b.mp3", "B renamed;jazz"));
  rescanAndWait();
  QCOMPARE(rows(playlist), (std::vector<int>{b, a}));
  QCOMPARE(dataChangedSpy.count(), 1);
  QCOMPARE(dataChangedSpy.at(0).at(0).toInt(), 0);

  // A song that stops matching leaves.
  QVERIFY(writeFile(root() + "/b.mp3", "B changed;rock"));
  rescanAndWait();
  QCOMPARE(rows(playlist), std::vector<int>{a});

  // Leaving the monitored library: the row goes before the song row.
  QStringList order;
  connect(&playlist, &Playlist::songsRemoved, this,
          [&]() { order << "row removed"; });
  connect(collection_, &PlaylistCollection::songsRemovedFromLibrary, this,
          [&]() { order << "song deleted"; });
  QVERIFY(QFile::remove(root() + "/a.mp3"));
  rescanAndWait();
  QVERIFY(playlist.empty());
  QCOMPARE(order, (QStringList{"row removed", "song deleted"}));
}

void TestAutoPlaylist::notifySongChanged_reevaluatesPlayStats() {
  QVERIFY(writeFile(root() + "/a.mp3", "A;jazz"));
  watchRoot();
  const int a = pkOf("a.mp3");
  const int id = collection_->createAutoPlaylist("Played", "play_count > 0");
  QVERIFY(collection_->playlist(id)->empty());

  QVERIFY(library_->incrementPlayCount(a));
  collection_->notifySongChanged(a);

  QCOMPARE(rows(*collection_->playlist(id)), std::vector<int>{a});
}

void TestAutoPlaylist::refreshPlaylistMetadata_updatesAutoPlaylists() {
  QVERIFY(writeFile(root() + "/a.mp3", "A;jazz"));
  QVERIFY(writeFile(root() + "/b.mp3", "B;jazz"));
  watchRoot();
  const int a = pkOf("a.mp3");
  const int b = pkOf("b.mp3");
  Playlist &manual = *collection_->playlist(1);
  QVERIFY(manual.addSongByPk(a));
  const int id = collection_->createAutoPlaylist("Jazz", "genre HAS jazz");
  Playlist &autoPlaylist = *collection_->playlist(id);
  QCOMPARE(rows(autoPlaylist), (std::vector<int>{a, b}));
  QSignalSpy refreshedSpy(&manual, &Playlist::songsRefreshed);

  // Refreshed from another playlist, without a scan.
  QVERIFY(writeFile(root() + "/a.mp3", "A;rock"));
  manual.refreshMetadataFromFiles();
  QCOMPARE(refreshedSpy.count(), 1);
  QCOMPARE(refreshedSpy.at(0).at(0).value<std::vector<int>>(),
           std::vector<int>{a});
  QCOMPARE(rows(autoPlaylist), std::vector<int>{b});

  // Refreshed from the autoplaylist itself, which loses the row.
  QVERIFY(writeFile(root() + "/b.mp3", "B;rock"));
  autoPlaylist.refreshMetadataFromFiles();
  QVERIFY(autoPlaylist.empty());
}

void TestAutoPlaylist::invalidExpression_hasNoRowsAndKeepsText() {
  QVERIFY(writeFile(root() + "/a.mp3", "A;jazz"));
  watchRoot();
  const int id = collection_->createAutoPlaylist("Jazz", "genre IN [pop");
  Playlist &playlist = *collection_->playlist(id);
  QVERIFY(playlist.empty());
  QVERIFY(!collection_->autoPlaylistError(id).isEmpty());
  QCOMPARE(collection_->autoPlaylistExpression(id), QString("genre IN [pop"));

  QVERIFY(collection_->setAutoPlaylistExpression(id, "davis"));
  QCOMPARE(collection_->autoPlaylistError(id),
           QString("Expression must be a condition"));
  QVERIFY(playlist.empty());

  QVERIFY(collection_->setAutoPlaylistExpression(id, "genre HAS jazz"));
  QVERIFY(collection_->autoPlaylistError(id).isEmpty());
  QCOMPARE(rows(playlist), std::vector<int>{pkOf("a.mp3")});
  QCOMPARE(queryInt(databaseManager_->db(),
                    QString("SELECT COUNT(*) FROM playlists WHERE "
                            "playlist_id=%1 AND expression='genre HAS jazz'")
                        .arg(id)),
           1);

  // An empty expression is invalid, and still stored.
  QVERIFY(collection_->setAutoPlaylistExpression(id, QString()));
  QVERIFY(!collection_->autoPlaylistError(id).isEmpty());
  QVERIFY(playlist.empty());

  QVERIFY(!collection_->setAutoPlaylistExpression(1, "true"));
  QVERIFY(!collection_->setAutoPlaylistExpression(99, "true"));
}

void TestAutoPlaylist::reloadAutoPlaylistExpressions_resolvesNewFields() {
  QVERIFY(writeFile(root() + "/a.mp3", "A;jazz"));
  watchRoot();
  const int id = collection_->createAutoPlaylist("All", "attr:mood IS \"\"");
  QVERIFY(!collection_->autoPlaylistError(id).isEmpty());

  registry_->addOrUpdateDynamicColumn({.id = "attr:mood", .title = "Mood"},
                                      {.id = "attr:mood",
                                       .source = ColumnSource::SongAttribute,
                                       .valueType = ValueType::Text,
                                       .searchable = true,
                                       .writable = true});
  collection_->reloadAutoPlaylistExpressions();

  QVERIFY(collection_->autoPlaylistError(id).isEmpty());
}

void TestAutoPlaylist::removeAutoPlaylist_releasesNothing() {
  QVERIFY(writeFile(root() + "/a.mp3", "A;jazz"));
  watchRoot();
  const int a = pkOf("a.mp3");
  const int id = collection_->createAutoPlaylist("Jazz", "genre HAS jazz");
  QSignalSpy releasedSpy(collection_,
                         &PlaylistCollection::songsRemovedFromLibrary);

  QVERIFY(collection_->removePlaylist(id));

  QCOMPARE(releasedSpy.count(), 0);
  QVERIFY(!collection_->isAuto(id));
  QVERIFY(!library_->getSongByPK(a).empty());
  QCOMPARE(queryInt(databaseManager_->db(),
                    QString("SELECT COUNT(*) FROM playlists WHERE "
                            "playlist_id=%1")
                        .arg(id)),
           0);
}

QTEST_GUILESS_MAIN(TestAutoPlaylist)
#include "tst_autoplaylist.moc"
