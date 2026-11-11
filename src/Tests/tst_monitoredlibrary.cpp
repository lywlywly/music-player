#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <atomic>
#include <filesystem>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../dummyfilesystemmonitor.h"
#include "../libraryscanner.h"
#include "../monitoredlibrary.h"
#include "../playlistcollection.h"
#include "../songlibrary.h"
#include "testsymlink.h"

namespace {
// Test files hold their title as text; "BAD" marks an unreadable file.
std::optional<MSong> fakeParse(const std::string &path) {
  QFile file(QString::fromStdString(path));
  if (!file.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }
  const QByteArray content = file.readAll().trimmed();
  if (content.startsWith("BAD")) {
    return std::nullopt;
  }
  MSong song;
  song.insert_or_assign("title", FieldValue(content.toStdString(), "title"));
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

std::vector<int> spyPks(const QSignalSpy &spy) {
  std::vector<int> pks;
  for (const QList<QVariant> &args : spy) {
    const auto batch = args.at(0).value<std::vector<int>>();
    pks.insert(pks.end(), batch.begin(), batch.end());
  }
  std::sort(pks.begin(), pks.end());
  return pks;
}
} // namespace

class TestMonitoredLibrary : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void newFile_createsRowAndEnters();
  void existingManualRow_isReusedAndRefreshed();
  void manualAddOfMonitoredFile_reusesPk();
  void unchangedFile_isNotParsed();
  void changedFile_isReparsed();
  void changedFileNowUnreadable_leavesAndIsRemembered();
  void unreadableNewFile_isSkippedUntilChanged();
  void goneFile_releasesUnlessManual();
  void movedFile_getsNewPk();
  void releasingManualReferences_keepsMonitoredRow();
  void removeFolder_releasesItsSongs();
  void offlineMissingRoot_releasesNothing();
  void offlineEmptyRoot_releasesNothingUntilRemoveMissing();
  void deletedFilesWithSubfolderLeft_folderStaysOnline();
  void walk_skipsHiddenSymlinksAndOtherExtensions();
  void addFolder_rejectsOverlapAndMissing();
  void requestsDuringScan_mergeIntoOneFollowUp();
  void rescanOfSeveralFolders_isOneScan();
  void removeFolderDuringScan_dropsItsResults();
  void manyFiles_applyInBatchesAndReportProgress();
  void staleResult_isSkippedAndRescanned();
  void noteSongWritten_avoidsReparse();
  void load_restoresFoldersAndFiles();
  void scanLibrary_pathScopeLimitsGoneAndClearedFailed();
  void watcherEvents_areDebouncedIntoOnePathScan();
  void watcherMissedEvents_rescanTheFolder();
  void offlineFolder_isUnwatchedUntilPollFindsItBack();
  void failedWatch_marksLiveUpdatesUnavailable();
  void decomposedFileName_staysAcrossScans();
  void folderOf_returnsTheHoldingFolder();

private:
  // A full scan of every folder, then waits until no scan is pending.
  void rescanAndWait();
  void waitIdle();
  QString root() const;
  bool offlinePollActive() const;

  QTemporaryDir *dir_ = nullptr;
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  DummyFileSystemMonitor *monitor_ = nullptr;
  MonitoredLibrary *monitored_ = nullptr;
  PlaylistCollection *collection_ = nullptr;
  QString connectionName_;
  std::atomic<int> parseCount_{0};
};

void TestMonitoredLibrary::init() {
  qRegisterMetaType<std::vector<int>>();
  dir_ = new QTemporaryDir();
  QVERIFY(dir_->isValid());
  connectionName_ =
      QStringLiteral("test_monitoredlibrary_%1")
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
  parseCount_ = 0;
  monitor_ = new DummyFileSystemMonitor();
  monitored_ = new MonitoredLibrary(
      *library_, *registry_, *databaseManager_,
      [this](const std::string &path, const ColumnRegistry &) {
        ++parseCount_;
        return fakeParse(path);
      },
      monitor_);
  monitored_->load();
  collection_ =
      new PlaylistCollection(*library_, *databaseManager_, *monitored_);
  collection_->loadAll();
}

void TestMonitoredLibrary::cleanup() {
  waitIdle();
  delete collection_;
  collection_ = nullptr;
  delete monitored_;
  monitored_ = nullptr;
  delete monitor_;
  monitor_ = nullptr;
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

QString TestMonitoredLibrary::root() const {
  return QFileInfo(dir_->path()).canonicalFilePath();
}

bool TestMonitoredLibrary::offlinePollActive() const {
  const QList<QTimer *> timers = monitored_->findChildren<QTimer *>();
  return std::any_of(timers.begin(), timers.end(), [](const QTimer *timer) {
    return timer->isActive() &&
           timer->interval() == MonitoredLibrary::kOfflinePollMs;
  });
}

void TestMonitoredLibrary::waitIdle() {
  QTRY_VERIFY_WITH_TIMEOUT(!monitored_->isScanning(), 5000);
}

void TestMonitoredLibrary::rescanAndWait() {
  monitored_->rescan();
  waitIdle();
}

void TestMonitoredLibrary::newFile_createsRowAndEnters() {
  QVERIFY(writeFile(root() + "/a.mp3", "Song A"));
  QSignalSpy enteredSpy(monitored_, &MonitoredLibrary::songsEntered);

  const MonitoredLibrary::AddFolderResult added = monitored_->addFolder(root());
  QVERIFY2(added.folderId > 0, qPrintable(added.error));
  waitIdle();

  QCOMPARE(monitored_->songPks().size(), size_t{1});
  const int pk = monitored_->songPks().front();
  QVERIFY(monitored_->holds(pk));
  QCOMPARE(spyPks(enteredSpy), std::vector<int>{pk});
  QCOMPARE(library_->getSongByPK(pk).at("title").text, std::string("Song A"));
  QCOMPARE(
      queryInt(databaseManager_->db(), "SELECT COUNT(*) FROM monitored_files"),
      1);
  QCOMPARE(queryInt(databaseManager_->db(),
                    QString("SELECT folder_id FROM monitored_files WHERE "
                            "song_id=%1")
                        .arg(pk)),
           added.folderId);
}

void TestMonitoredLibrary::existingManualRow_isReusedAndRefreshed() {
  const QString path = root() + "/a.mp3";
  QVERIFY(writeFile(path, "Old"));
  Playlist *playlist = collection_->playlist(1);
  playlist->addFiles({path.toStdString()});
  const int pk = playlist->getPkByIndex(0);
  QVERIFY(writeFile(path, "New title"));

  QSignalSpy dataChangedSpy(playlist, &Playlist::songDataChanged);
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  waitIdle();

  QCOMPARE(monitored_->songPks(), std::vector<int>{pk});
  QCOMPARE(library_->getSongByPK(pk).at("title").text,
           std::string("New title"));
  QCOMPARE(dataChangedSpy.count(), 1);
}

void TestMonitoredLibrary::manualAddOfMonitoredFile_reusesPk() {
  const QString path = root() + "/a.mp3";
  QVERIFY(writeFile(path, "A"));
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  waitIdle();
  const int pk = monitored_->songPks().front();

  const std::vector<int> added =
      collection_->playlist(1)->addFiles({path.toStdString()});
  QCOMPARE(added, std::vector<int>{pk});
  QCOMPARE(monitored_->songPks(), std::vector<int>{pk});
}

void TestMonitoredLibrary::unchangedFile_isNotParsed() {
  QVERIFY(writeFile(root() + "/a.mp3", "A"));
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  waitIdle();
  QCOMPARE(parseCount_.load(), 1);

  QSignalSpy enteredSpy(monitored_, &MonitoredLibrary::songsEntered);
  QSignalSpy updatedSpy(monitored_, &MonitoredLibrary::songsUpdated);
  QSignalSpy leftSpy(monitored_, &MonitoredLibrary::songsLeft);
  QSignalSpy finishedSpy(monitored_, &MonitoredLibrary::scanFinished);
  rescanAndWait();

  QCOMPARE(parseCount_.load(), 1);
  QCOMPARE(enteredSpy.count(), 0);
  QCOMPARE(updatedSpy.count(), 0);
  QCOMPARE(leftSpy.count(), 0);
  QCOMPARE(finishedSpy.count(), 1);
}

void TestMonitoredLibrary::changedFile_isReparsed() {
  const QString path = root() + "/a.mp3";
  QVERIFY(writeFile(path, "A"));
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  waitIdle();
  const int pk = monitored_->songPks().front();
  Playlist *playlist = collection_->playlist(1);
  playlist->addSongByPk(pk);

  // Same size, different mtime.
  QVERIFY(writeFile(path, "B"));
  std::filesystem::last_write_time(
      path.toStdString(), std::filesystem::last_write_time(path.toStdString()) +
                              std::chrono::seconds(5));
  QSignalSpy updatedSpy(monitored_, &MonitoredLibrary::songsUpdated);
  QSignalSpy dataChangedSpy(playlist, &Playlist::songDataChanged);
  rescanAndWait();

  QCOMPARE(spyPks(updatedSpy), std::vector<int>{pk});
  QCOMPARE(dataChangedSpy.count(), 1);
  QCOMPARE(library_->getSongByPK(pk).at("title").text, std::string("B"));
  const std::optional<FileStats> stats = readFileStats(path);
  QVERIFY(stats.has_value());
  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec(QString("SELECT mtime, size FROM monitored_files WHERE "
                         "song_id=%1")
                     .arg(pk)));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toLongLong(), stats->mtimeNs);
  QCOMPARE(q.value(1).toLongLong(), stats->size);
}

void TestMonitoredLibrary::changedFileNowUnreadable_leavesAndIsRemembered() {
  const QString path = root() + "/a.mp3";
  QVERIFY(writeFile(path, "A"));
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  waitIdle();
  const int pk = monitored_->songPks().front();

  QVERIFY(writeFile(path, "BAD file"));
  QSignalSpy leftSpy(monitored_, &MonitoredLibrary::songsLeft);
  QSignalSpy removedSpy(collection_,
                        &PlaylistCollection::songsRemovedFromLibrary);
  rescanAndWait();
  QCOMPARE(spyPks(leftSpy), std::vector<int>{pk});
  QCOMPARE(spyPks(removedSpy), std::vector<int>{pk});
  QVERIFY(monitored_->songPks().empty());
  QVERIFY(library_->getSongByPK(pk).empty());

  const int parsed = parseCount_.load();
  rescanAndWait();
  QCOMPARE(parseCount_.load(), parsed);
}

void TestMonitoredLibrary::unreadableNewFile_isSkippedUntilChanged() {
  const QString path = root() + "/a.mp3";
  QVERIFY(writeFile(path, "BAD"));
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  waitIdle();
  QVERIFY(monitored_->songPks().empty());
  QCOMPARE(parseCount_.load(), 1);

  rescanAndWait();
  QCOMPARE(parseCount_.load(), 1);

  QVERIFY(writeFile(path, "Readable now"));
  rescanAndWait();
  QCOMPARE(parseCount_.load(), 2);
  QCOMPARE(monitored_->songPks().size(), size_t{1});
}

void TestMonitoredLibrary::goneFile_releasesUnlessManual() {
  QVERIFY(writeFile(root() + "/a.mp3", "A"));
  QVERIFY(writeFile(root() + "/b.mp3", "B"));
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  waitIdle();
  const std::vector<int> pks = monitored_->songPks();
  QCOMPARE(pks.size(), size_t{2});
  const int pkA =
      library_->getSongByPK(pks[0]).at("title").text == "A" ? pks[0] : pks[1];
  const int pkB = pkA == pks[0] ? pks[1] : pks[0];
  collection_->playlist(1)->addSongByPk(pkB);

  QVERIFY(QFile::remove(root() + "/a.mp3"));
  QVERIFY(QFile::remove(root() + "/b.mp3"));
  QVERIFY(writeFile(root() + "/keep.txt", "keeps the root non-empty"));
  QSignalSpy leftSpy(monitored_, &MonitoredLibrary::songsLeft);
  QSignalSpy removedSpy(collection_,
                        &PlaylistCollection::songsRemovedFromLibrary);
  rescanAndWait();

  QCOMPARE(spyPks(leftSpy),
           (std::vector<int>{std::min(pkA, pkB), std::max(pkA, pkB)}));
  QCOMPARE(spyPks(removedSpy), std::vector<int>{pkA});
  QVERIFY(library_->getSongByPK(pkA).empty());
  // M+W -> M: kept, still in the manual playlist.
  QVERIFY(!library_->getSongByPK(pkB).empty());
  QVERIFY(collection_->playlist(1)->containsPk(pkB));
  QVERIFY(monitored_->songPks().empty());
}

void TestMonitoredLibrary::movedFile_getsNewPk() {
  QVERIFY(writeFile(root() + "/a/x.mp3", "X"));
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  waitIdle();
  const int oldPk = monitored_->songPks().front();
  const std::string identity =
      library_->getSongByPK(oldPk).at("song_identity_key").text;

  QVERIFY(QDir().mkpath(root() + "/b"));
  QVERIFY(QFile::rename(root() + "/a/x.mp3", root() + "/b/x.mp3"));
  rescanAndWait();

  QCOMPARE(monitored_->songPks().size(), size_t{1});
  const int newPk = monitored_->songPks().front();
  QVERIFY(newPk != oldPk);
  QVERIFY(library_->getSongByPK(oldPk).empty());
  QCOMPARE(library_->getSongByPK(newPk).at("song_identity_key").text, identity);
}

void TestMonitoredLibrary::releasingManualReferences_keepsMonitoredRow() {
  QVERIFY(writeFile(root() + "/a.mp3", "A"));
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  waitIdle();
  const int pk = monitored_->songPks().front();
  QSignalSpy removedSpy(collection_,
                        &PlaylistCollection::songsRemovedFromLibrary);

  // Through Playlist::songPksRemoved.
  Playlist *playlist = collection_->playlist(1);
  playlist->addSongByPk(pk);
  playlist->clear();
  // Through removePlaylist.
  QVERIFY(collection_->removePlaylist(collection_->createPlaylist("P", {pk})));

  QCOMPARE(removedSpy.count(), 0);
  QVERIFY(!library_->getSongByPK(pk).empty());
  QVERIFY(monitored_->holds(pk));
}

void TestMonitoredLibrary::removeFolder_releasesItsSongs() {
  QVERIFY(writeFile(root() + "/w/a.mp3", "A"));
  QVERIFY(writeFile(root() + "/w/b.mp3", "B"));
  const MonitoredLibrary::AddFolderResult added =
      monitored_->addFolder(root() + "/w");
  QVERIFY(added.folderId > 0);
  waitIdle();
  const std::vector<int> pks = monitored_->songPks();
  QCOMPARE(pks.size(), size_t{2});
  collection_->playlist(1)->addSongByPk(pks[1]);

  QSignalSpy leftSpy(monitored_, &MonitoredLibrary::songsLeft);
  QSignalSpy removedSpy(collection_,
                        &PlaylistCollection::songsRemovedFromLibrary);
  QVERIFY(monitored_->removeFolder(added.folderId));
  QVERIFY(!monitored_->removeFolder(added.folderId));

  QCOMPARE(spyPks(leftSpy), pks);
  QCOMPARE(spyPks(removedSpy), std::vector<int>{pks[0]});
  QVERIFY(!library_->getSongByPK(pks[1]).empty());
  QVERIFY(monitored_->folders().empty());
  QVERIFY(monitored_->songPks().empty());
  QCOMPARE(
      queryInt(databaseManager_->db(), "SELECT COUNT(*) FROM monitored_files"),
      0);
  QCOMPARE(
      queryInt(databaseManager_->db(), "SELECT COUNT(*) FROM library_folders"),
      0);
}

void TestMonitoredLibrary::offlineMissingRoot_releasesNothing() {
  QVERIFY(writeFile(root() + "/w/a.mp3", "A"));
  const int folderId = monitored_->addFolder(root() + "/w").folderId;
  QVERIFY(folderId > 0);
  waitIdle();
  const int pk = monitored_->songPks().front();

  QVERIFY(QDir().rename(root() + "/w", root() + "/unmounted"));
  QSignalSpy leftSpy(monitored_, &MonitoredLibrary::songsLeft);
  QSignalSpy availabilitySpy(monitored_,
                             &MonitoredLibrary::folderAvailabilityChanged);
  rescanAndWait();
  QCOMPARE(leftSpy.count(), 0);
  QVERIFY(monitored_->holds(pk));
  QVERIFY(!library_->getSongByPK(pk).empty());
  QCOMPARE(availabilitySpy.count(), 1);
  QCOMPARE(availabilitySpy.at(0).at(0).toInt(), folderId);
  QCOMPARE(availabilitySpy.at(0).at(1).toBool(), false);
  QVERIFY(!monitored_->folders().front().online);

  QVERIFY(QDir().rename(root() + "/unmounted", root() + "/w"));
  rescanAndWait();
  QCOMPARE(availabilitySpy.count(), 2);
  QCOMPARE(availabilitySpy.at(1).at(1).toBool(), true);
  QVERIFY(monitored_->folders().front().online);
  QCOMPARE(monitored_->songPks(), std::vector<int>{pk});
  QCOMPARE(leftSpy.count(), 0);
}

void TestMonitoredLibrary::
    offlineEmptyRoot_releasesNothingUntilRemoveMissing() {
  QVERIFY(writeFile(root() + "/w/a.mp3", "A"));
  const int folderId = monitored_->addFolder(root() + "/w").folderId;
  QVERIFY(folderId > 0);
  waitIdle();
  const int pk = monitored_->songPks().front();

  QVERIFY(QFile::remove(root() + "/w/a.mp3"));
  QSignalSpy leftSpy(monitored_, &MonitoredLibrary::songsLeft);
  rescanAndWait();
  QCOMPARE(leftSpy.count(), 0);
  QVERIFY(monitored_->holds(pk));
  QVERIFY(!monitored_->folders().front().online);
  QVERIFY(monitor_->watchedPaths.isEmpty());

  QSignalSpy removedSpy(collection_,
                        &PlaylistCollection::songsRemovedFromLibrary);
  monitored_->removeMissingSongs(folderId);
  waitIdle();
  QCOMPARE(spyPks(removedSpy), std::vector<int>{pk});
  QVERIFY(monitored_->songPks().empty());
  QVERIFY(monitored_->folders().front().online);
  QCOMPARE(monitor_->watchedPaths, QStringList{root() + "/w"});

  // Still empty, but with no files in the library: online.
  rescanAndWait();
  QVERIFY(monitored_->folders().front().online);
}

void TestMonitoredLibrary::deletedFilesWithSubfolderLeft_folderStaysOnline() {
  QVERIFY(writeFile(root() + "/w/a.mp3", "A"));
  QVERIFY(QDir().mkpath(root() + "/w/sub"));
  QVERIFY(monitored_->addFolder(root() + "/w").folderId > 0);
  waitIdle();
  const int pk = monitored_->songPks().front();

  QVERIFY(QFile::remove(root() + "/w/a.mp3"));
  QSignalSpy leftSpy(monitored_, &MonitoredLibrary::songsLeft);
  rescanAndWait();
  QCOMPARE(spyPks(leftSpy), std::vector<int>{pk});
  QVERIFY(monitored_->folders().front().online);
}

void TestMonitoredLibrary::walk_skipsHiddenSymlinksAndOtherExtensions() {
  const QString w = root() + "/w";
  QVERIFY(writeFile(w + "/a.mp3", "A"));
  QVERIFY(writeFile(w + "/B.MP3", "B"));
  QVERIFY(writeFile(w + "/sub/c.FlAc", "C"));
  QVERIFY(writeFile(w + "/notes.txt", "N"));
  QVERIFY(writeFile(w + "/noext", "N"));
  QVERIFY(writeFile(w + "/.hidden.mp3", "H"));
  QVERIFY(writeFile(w + "/._a.mp3", "H"));
  QVERIFY(writeFile(w + "/.hiddendir/d.mp3", "H"));
  QVERIFY(writeFile(root() + "/outside/e.mp3", "E"));
  // Where Windows refuses symlinks the expected songs are the same; the links
  // are then not exercised.
  makeSymlink(root() + "/outside/e.mp3", w + "/link.mp3");
  makeSymlink(root() + "/outside", w + "/linkdir");

  QVERIFY(monitored_->addFolder(w).folderId > 0);
  waitIdle();

  QStringList titles;
  for (int pk : monitored_->songPks()) {
    titles.push_back(
        QString::fromStdString(library_->getSongByPK(pk).at("title").text));
  }
  titles.sort();
  QCOMPARE(titles, (QStringList{"A", "B", "C"}));
}

void TestMonitoredLibrary::addFolder_rejectsOverlapAndMissing() {
  QVERIFY(QDir().mkpath(root() + "/music/sub"));
  QVERIFY(QDir().mkpath(root() + "/music2"));
  const bool linked = makeSymlink(root() + "/music", root() + "/alias");

  QVERIFY(monitored_->addFolder(root() + "/music").folderId > 0);
  waitIdle();

  const auto rejected = [this](const QString &path) {
    const MonitoredLibrary::AddFolderResult result =
        monitored_->addFolder(path);
    return result.folderId == -1 && !result.error.isEmpty();
  };
  QVERIFY(rejected(root() + "/music"));
  QVERIFY(rejected(root() + "/music/"));
  if (linked) {
    QVERIFY(rejected(root() + "/alias"));
  }
  QVERIFY(rejected(root() + "/music/sub"));
  QVERIFY(rejected(root()));
  QVERIFY(rejected(root() + "/missing"));
  QVERIFY(writeFile(root() + "/file.mp3", "F"));
  QVERIFY(rejected(root() + "/file.mp3"));
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
  QVERIFY(rejected(root() + "/MUSIC/sub"));
#endif

  // A shared name prefix is not overlap.
  QVERIFY(monitored_->addFolder(root() + "/music2").folderId > 0);
  waitIdle();
  QCOMPARE(monitored_->folders().size(), size_t{2});
}

void TestMonitoredLibrary::requestsDuringScan_mergeIntoOneFollowUp() {
  QVERIFY(writeFile(root() + "/a.mp3", "A"));
  QSignalSpy finishedSpy(monitored_, &MonitoredLibrary::scanFinished);
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  // The first scan is running until the event loop applies it.
  QVERIFY(monitored_->isScanning());
  monitored_->rescan();
  monitored_->scanPaths({root() + "/a.mp3"});
  monitored_->rescan();
  waitIdle();
  QCOMPARE(finishedSpy.count(), 2);
  QCOMPARE(parseCount_.load(), 1);
}

void TestMonitoredLibrary::rescanOfSeveralFolders_isOneScan() {
  QVERIFY(writeFile(root() + "/x/a.mp3", "A"));
  QVERIFY(writeFile(root() + "/y/b.mp3", "B"));
  QVERIFY(monitored_->addFolder(root() + "/x").folderId > 0);
  QVERIFY(monitored_->addFolder(root() + "/y").folderId > 0);
  waitIdle();

  QSignalSpy finishedSpy(monitored_, &MonitoredLibrary::scanFinished);
  rescanAndWait();
  QCOMPARE(finishedSpy.count(), 1);
}

void TestMonitoredLibrary::removeFolderDuringScan_dropsItsResults() {
  QVERIFY(writeFile(root() + "/a.mp3", "A"));
  const int folderId = monitored_->addFolder(root()).folderId;
  QVERIFY(monitored_->isScanning());
  QVERIFY(monitored_->removeFolder(folderId));
  QSignalSpy enteredSpy(monitored_, &MonitoredLibrary::songsEntered);
  waitIdle();
  QCOMPARE(enteredSpy.count(), 0);
  QVERIFY(monitored_->songPks().empty());
  QCOMPARE(
      queryInt(databaseManager_->db(), "SELECT COUNT(*) FROM monitored_files"),
      0);
}

void TestMonitoredLibrary::manyFiles_applyInBatchesAndReportProgress() {
  const int count = MonitoredLibrary::kApplyBatchSize + 1;
  for (int i = 0; i < count; ++i) {
    QVERIFY(writeFile(root() + QString("/%1.mp3").arg(i, 3, 10, QChar('0')),
                      QByteArray::number(i)));
  }
  QSignalSpy enteredSpy(monitored_, &MonitoredLibrary::songsEntered);
  QSignalSpy progressSpy(monitored_, &MonitoredLibrary::scanProgress);
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  waitIdle();

  QCOMPARE(enteredSpy.count(), 2);
  QCOMPARE(enteredSpy.at(0).at(0).value<std::vector<int>>().size(),
           size_t(MonitoredLibrary::kApplyBatchSize));
  QCOMPARE(spyPks(enteredSpy).size(), size_t(count));
  QCOMPARE(monitored_->songPks().size(), size_t(count));
  QVERIFY(!progressSpy.isEmpty());
  QCOMPARE(progressSpy.last().at(0).toInt(), count);
  QCOMPARE(progressSpy.last().at(1).toInt(), count);
}

void TestMonitoredLibrary::staleResult_isSkippedAndRescanned() {
  const QString path = root() + "/a.mp3";
  QVERIFY(writeFile(path, "A"));
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  waitIdle();
  const int pk = monitored_->songPks().front();

  QVERIFY(writeFile(path, "AB"));
  QSignalSpy updatedSpy(monitored_, &MonitoredLibrary::songsUpdated);
  QSignalSpy finishedSpy(monitored_, &MonitoredLibrary::scanFinished);
  monitored_->rescan();
  // The app saves tags while the scan runs.
  QVERIFY(writeFile(path, "ABC"));
  monitored_->noteSongWritten(pk);
  waitIdle();

  QCOMPARE(updatedSpy.count(), 0);
  QCOMPARE(finishedSpy.count(), 2);
  QCOMPARE(library_->getSongByPK(pk).at("title").text, std::string("A"));
}

void TestMonitoredLibrary::noteSongWritten_avoidsReparse() {
  const QString path = root() + "/a.mp3";
  QVERIFY(writeFile(path, "A"));
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  waitIdle();
  const int pk = monitored_->songPks().front();

  QVERIFY(writeFile(path, "Saved by the app"));
  monitored_->noteSongWritten(pk);
  // Not monitored: nothing happens.
  monitored_->noteSongWritten(pk + 100);
  const int parsed = parseCount_.load();
  QSignalSpy updatedSpy(monitored_, &MonitoredLibrary::songsUpdated);
  rescanAndWait();
  QCOMPARE(parseCount_.load(), parsed);
  QCOMPARE(updatedSpy.count(), 0);
}

void TestMonitoredLibrary::load_restoresFoldersAndFiles() {
  QVERIFY(writeFile(root() + "/a.mp3", "A"));
  const int folderId = monitored_->addFolder(root()).folderId;
  waitIdle();
  const std::vector<int> pks = monitored_->songPks();

  DummyFileSystemMonitor reloadedMonitor;
  MonitoredLibrary reloaded(
      *library_, *registry_, *databaseManager_,
      [this](const std::string &path, const ColumnRegistry &) {
        ++parseCount_;
        return fakeParse(path);
      },
      &reloadedMonitor);
  reloaded.load();
  QCOMPARE(reloadedMonitor.watchedPaths, QStringList{root()});
  QCOMPARE(reloaded.songPks(), pks);
  QCOMPARE(reloaded.folders().size(), size_t{1});
  QCOMPARE(reloaded.folders().front().id, folderId);
  QCOMPARE(reloaded.folders().front().path, root());

  const int parsed = parseCount_.load();
  reloaded.rescan();
  QTRY_VERIFY_WITH_TIMEOUT(!reloaded.isScanning(), 5000);
  QCOMPARE(parseCount_.load(), parsed);
}

void TestMonitoredLibrary::scanLibrary_pathScopeLimitsGoneAndClearedFailed() {
  const QString w = root() + "/w";
  QVERIFY(writeFile(w + "/x/new.mp3", "N"));
  QVERIFY(writeFile(w + "/y/kept.mp3", "K"));

  LibraryScanInput input;
  input.folders.push_back(ScanFolderInput{
      .folderId = 1, .path = w, .scopes = {w + "/x"}, .hasFiles = true});
  // Missing on disk, inside and outside the scope.
  input.snapshot.insert(w + "/x/old.mp3",
                        ScanSnapshotEntry{.pk = 7, .folderId = 1});
  input.snapshot.insert(w + "/z/other.mp3",
                        ScanSnapshotEntry{.pk = 8, .folderId = 1});
  // Failed before: now missing, and outside the scope.
  input.failed.insert(w + "/x/deleted.mp3", FileStats{});
  input.failed.insert(w + "/z/elsewhere.mp3", FileStats{});
  const LibraryScanResult result =
      scanLibrary(input, [](const std::string &path, const ColumnRegistry &) {
        return fakeParse(path);
      });

  QCOMPARE(result.availability.size(), size_t{1});
  QVERIFY(result.availability.front().online);
  QCOMPARE(result.entries.size(), size_t{2});
  QCOMPARE(result.entries[0].path, w + "/x/new.mp3");
  QCOMPARE(result.entries[0].kind, ScanEntry::Kind::New);
  QCOMPARE(result.entries[1].path, w + "/x/old.mp3");
  QCOMPARE(result.entries[1].kind, ScanEntry::Kind::Gone);
  QCOMPARE(result.entries[1].pk, 7);
  QCOMPARE(result.clearedFailed, QStringList{w + "/x/deleted.mp3"});
}

void TestMonitoredLibrary::watcherEvents_areDebouncedIntoOnePathScan() {
  const int folderId = monitored_->addFolder(root()).folderId;
  QVERIFY(folderId > 0);
  waitIdle();
  QCOMPARE(monitor_->watchedPaths, QStringList{root()});

  // c.mp3 gets no event, so the path scan doesn't find it.
  for (const QString name : {"a", "b", "c"}) {
    QVERIFY(writeFile(root() + "/" + name + ".mp3", name.toUtf8()));
  }
  QSignalSpy finishedSpy(monitored_, &MonitoredLibrary::scanFinished);
  emit monitor_->pathsChanged({root() + "/a.mp3"});
  emit monitor_->pathsChanged({root() + "/b.mp3", "/elsewhere/d.mp3"});
  QVERIFY(!monitored_->isScanning());
  QTRY_COMPARE(monitored_->songPks().size(), size_t{2});
  QCOMPARE(finishedSpy.count(), 1);

  // A path that no longer exists is scanned as gone.
  QVERIFY(QFile::remove(root() + "/a.mp3"));
  emit monitor_->pathsChanged({root() + "/a.mp3"});
  QTRY_COMPARE(monitored_->songPks().size(), size_t{1});

  QVERIFY(monitored_->removeFolder(folderId));
  QVERIFY(monitor_->watchedPaths.isEmpty());
}

void TestMonitoredLibrary::watcherMissedEvents_rescanTheFolder() {
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  waitIdle();
  QVERIFY(writeFile(root() + "/a.mp3", "A"));
  QVERIFY(writeFile(root() + "/sub/b.mp3", "B"));

  emit monitor_->folderNeedsRescan(root() + "/sub");
  QVERIFY(monitored_->isScanning());
  waitIdle();
  QCOMPARE(monitored_->songPks().size(), size_t{2});
}

void TestMonitoredLibrary::offlineFolder_isUnwatchedUntilPollFindsItBack() {
  const QString folder = root() + "/music";
  const QString away = root() + "/away";
  QVERIFY(writeFile(folder + "/a.mp3", "A"));
  const int folderId = monitored_->addFolder(folder).folderId;
  QVERIFY(folderId > 0);
  waitIdle();

  QSignalSpy availabilitySpy(monitored_,
                             &MonitoredLibrary::folderAvailabilityChanged);
  QVERIFY(QDir().rename(folder, away));
  rescanAndWait();
  QCOMPARE(availabilitySpy.count(), 1);
  QCOMPARE(availabilitySpy.takeFirst(), QVariantList({folderId, false}));
  QVERIFY(offlinePollActive());

  // Still missing, then an empty mount point (case 2).
  monitored_->checkOfflineFolders();
  QVERIFY(QDir().mkpath(folder));
  monitored_->checkOfflineFolders();
  QCOMPARE(availabilitySpy.count(), 0);
  QVERIFY(!monitored_->isScanning());

  QVERIFY(QDir(folder).removeRecursively());
  QVERIFY(QDir().rename(away, folder));
  monitored_->checkOfflineFolders();
  QCOMPARE(availabilitySpy.count(), 1);
  QCOMPARE(availabilitySpy.takeFirst(), QVariantList({folderId, true}));
  QCOMPARE(monitor_->watchedPaths, QStringList{folder});
  QVERIFY(!offlinePollActive());
  QVERIFY(monitored_->isScanning());
  waitIdle();
  QCOMPARE(monitored_->songPks().size(), size_t{1});
  QCOMPARE(availabilitySpy.count(), 0);
}

void TestMonitoredLibrary::failedWatch_marksLiveUpdatesUnavailable() {
  const QString folder = root() + "/music";
  QVERIFY(writeFile(folder + "/a.mp3", "A"));
  monitor_->refusedPaths = {folder};
  QVERIFY(monitored_->addFolder(folder).folderId > 0);
  QVERIFY(monitored_->folders().front().liveUpdatesUnavailable);
  // Scans still work.
  waitIdle();
  QCOMPARE(monitored_->songPks().size(), size_t{1});
  QVERIFY(monitor_->watchedPaths.isEmpty());

  // A watch that fails because the root is missing is not marked.
  QVERIFY(QDir().rename(folder, root() + "/away"));
  DummyFileSystemMonitor reloadedMonitor;
  reloadedMonitor.refusedPaths = {folder};
  MonitoredLibrary reloaded(
      *library_, *registry_, *databaseManager_,
      [](const std::string &path, const ColumnRegistry &) {
        return fakeParse(path);
      },
      &reloadedMonitor);
  reloaded.load();
  QVERIFY(!reloaded.folders().front().liveUpdatesUnavailable);
}

// See "Scan" in docs/monitored-library.md (composed paths).
void TestMonitoredLibrary::decomposedFileName_staysAcrossScans() {
  // "が" decomposed (NFD): か + combining dakuten, as macOS can store names.
  const std::filesystem::path file =
      std::filesystem::path(root().toStdString()) /
      "\xE3\x81\x8B\xE3\x82\x99.mp3";
  QVERIFY(writeFile(QString::fromStdString(file.string()), "Song"));
  QVERIFY(monitored_->addFolder(root()).folderId > 0);
  waitIdle();
  QCOMPARE(monitored_->songPks().size(), size_t{1});
  const int pk = monitored_->songPks().front();

  // The NFD path on disk matches the stored NFC path.
  rescanAndWait();
  QCOMPARE(monitored_->songPks(), std::vector<int>{pk});
}

void TestMonitoredLibrary::folderOf_returnsTheHoldingFolder() {
  QVERIFY(writeFile(root() + "/w1/a.mp3", "A"));
  QVERIFY(writeFile(root() + "/w2/b.mp3", "B"));
  const int w1 = monitored_->addFolder(root() + "/w1").folderId;
  const int w2 = monitored_->addFolder(root() + "/w2").folderId;
  QVERIFY(w1 > 0 && w2 > 0);
  waitIdle();
  int a = -1;
  int b = -1;
  for (int pk : monitored_->songPks()) {
    (library_->getSongByPK(pk).at("title").text == "A" ? a : b) = pk;
  }
  QVERIFY(a >= 0 && b >= 0);

  QCOMPARE(monitored_->folderOf(a)->id, w1);
  QCOMPARE(monitored_->folderOf(a)->path, root() + "/w1");
  QVERIFY(monitored_->folderOf(a)->online);
  QCOMPARE(monitored_->folderOf(b)->id, w2);
  QVERIFY(!monitored_->folderOf(-1).has_value());

  // Offline folders are still returned, marked offline.
  QVERIFY(QDir().rename(root() + "/w1", root() + "/unmounted"));
  rescanAndWait();
  QVERIFY(!monitored_->folderOf(a)->online);
  QVERIFY(monitored_->folderOf(b)->online);

  // A manual song whose folder was removed is held by no folder.
  QVERIFY(collection_->playlist(collection_->playlistIdsInOrder().front())
              ->addSongByPk(b));
  QVERIFY(monitored_->removeFolder(w2));
  QVERIFY(!monitored_->folderOf(b).has_value());
}

QTEST_GUILESS_MAIN(TestMonitoredLibrary)
#include "tst_monitoredlibrary.moc"
