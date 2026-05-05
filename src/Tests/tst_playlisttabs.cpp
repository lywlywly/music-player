#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QGuiApplication>
#include <QObject>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../globalcolumnlayoutmanager.h"
#include "../playbackmanager.h"
#include "../playbackqueue.h"
#include "../playlisttabs.h"
#include "../songlibrary.h"
#include "../utils.h"

namespace {
MSong makeSong(const QString &title, const QString &artist, const QString &path,
               const QString &tracknumber = {}) {
  MSong song;
  song["title"] = title.toStdString();
  song["artist"] = artist.toStdString();
  song["album"] = "Album";
  song["discnumber"] = FieldValue("1", ColumnValueType::Number);
  if (!tracknumber.isEmpty()) {
    song["tracknumber"] =
        FieldValue(tracknumber.toStdString(), ColumnValueType::Number);
  }
  song["date"] = FieldValue("2024-01-01", ColumnValueType::DateTime);
  song["genre"] = "genre";
  song["filepath"] = path.toStdString();
  const std::string identity =
      util::normalizedText(title).toStdString() + "|" +
      util::normalizedText(artist).toStdString() + "|" +
      util::normalizedText(QStringLiteral("Album")).toStdString();
  song["song_identity_key"] = identity;
  return song;
}

QActionGroup *makePlaybackOrderGroup(QObject *parent = nullptr) {
  auto *group = new QActionGroup(parent);
  group->setExclusive(true);

  QAction *defaultAction = new QAction("Default", group);
  defaultAction->setCheckable(true);
  defaultAction->setData(Policy::Sequential);
  defaultAction->setChecked(true);
  group->addAction(defaultAction);

  QAction *shuffleAction = new QAction("Shuffle (tracks)", group);
  shuffleAction->setCheckable(true);
  shuffleAction->setData(Policy::Shuffle);
  group->addAction(shuffleAction);
  return group;
}

int insertPlaylistRow(QSqlDatabase &db, const QString &name, int tabOrder) {
  QSqlQuery q(db);
  q.prepare(R"(
      INSERT INTO playlists(name, last_played, tab_order)
      VALUES(:name, 1, :tab_order)
  )");
  q.bindValue(":name", name);
  q.bindValue(":tab_order", tabOrder);
  if (!q.exec()) {
    return -1;
  }
  return q.lastInsertId().toInt();
}

bool updatePlaylistTabOrder(QSqlDatabase &db, const QList<int> &playlistIds) {
  if (playlistIds.isEmpty()) {
    return true;
  }
  if (!db.transaction()) {
    return false;
  }
  QSqlQuery q(db);
  q.prepare(R"(
      UPDATE playlists
      SET tab_order=:tab_order
      WHERE playlist_id=:playlist_id
  )");
  for (int i = 0; i < playlistIds.size(); ++i) {
    q.bindValue(":tab_order", i);
    q.bindValue(":playlist_id", playlistIds[i]);
    if (!q.exec()) {
      db.rollback();
      return false;
    }
  }
  if (!db.commit()) {
    db.rollback();
    return false;
  }
  return true;
}
} // namespace

class TestPlaylistTabs : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void init_createsDefaultPlaylistAndActions();
  void init_persistsDefaultPlaylistMetadataRow();
  void helperMethods_mapAndFindPlaylist();
  void navigateIndex_selectsTargetRow();
  void notifySongDataChangedInAllPlaylists_notifiesMatchingRows();
  void customContextMenu_bindsRowIntoQueueActions();
  void createNewPlaylistTabFromSongIds_createsTabAndCopiesSongs();
  void createNewPlaylistTabFromSongIds_emptyNoop();
  void startupRestore_loadsAllPlaylistsInSavedOrder();
  void startupRestore_restoresLastOpenedPlaylistTab();
  void startupRestore_recoversCorruptedTabOrder();
  void removeTab_deletesNonDefaultPlaylist();
  void removeTab_rejectsWhenSingleTab();
  void removeTab_invalidIndexRejected();
  void removeTab_allowsRemovingDefaultWhenMultipleTabs();
  void tabMove_persistsOrderInDb();
  void renameTab_updatesTabTextAndDb();

private:
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  GlobalColumnLayoutManager *layout_ = nullptr;
  PlaybackQueue *queue_ = nullptr;
  PlaybackManager *manager_ = nullptr;
  QActionGroup *playbackOrderGroup_ = nullptr;
  PlaylistTabs *tabs_ = nullptr;
  QString connectionName_;
};

void TestPlaylistTabs::init() {
  QCoreApplication::setOrganizationName("music-player-tests");
  QCoreApplication::setApplicationName("playlisttabs-tests");
  QSettings settings;
  settings.clear();

  connectionName_ =
      QStringLiteral("test_playlisttabs_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
  layout_ = new GlobalColumnLayoutManager(*registry_);
  queue_ = new PlaybackQueue();
  manager_ = new PlaybackManager(*queue_);
  playbackOrderGroup_ = makePlaybackOrderGroup();
  tabs_ = new PlaylistTabs();
  tabs_->init(library_, queue_, manager_, playbackOrderGroup_, registry_,
              layout_, databaseManager_);
}

void TestPlaylistTabs::cleanup() {
  delete tabs_;
  tabs_ = nullptr;
  delete playbackOrderGroup_;
  playbackOrderGroup_ = nullptr;
  delete manager_;
  manager_ = nullptr;
  delete queue_;
  queue_ = nullptr;
  delete layout_;
  layout_ = nullptr;
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
}

void TestPlaylistTabs::init_createsDefaultPlaylistAndActions() {
  QVERIFY(tabs_->currentPlaylist() != nullptr);
  QVERIFY(tabs_->playNextAction() != nullptr);
  QVERIFY(tabs_->playEndAction() != nullptr);
  QCOMPARE(tabs_->tabWidget()->count(), 1);
  QCOMPARE(tabs_->tabWidget()->tabText(0), QString("Default"));
}

void TestPlaylistTabs::init_persistsDefaultPlaylistMetadataRow() {
  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("SELECT name, tab_order FROM playlists WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("Default"));
  QCOMPARE(q.value(1).toInt(), 0);
}

void TestPlaylistTabs::helperMethods_mapAndFindPlaylist() {
  QAction *defaultAction = playbackOrderGroup_->actions().at(0);
  QAction *shuffleAction = playbackOrderGroup_->actions().at(1);
  QCOMPARE(defaultAction->data().toInt(), static_cast<int>(Policy::Sequential));
  QCOMPARE(shuffleAction->data().toInt(), static_cast<int>(Policy::Shuffle));

  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  QCOMPARE(QString::fromStdString(tabs_->findPlaylistName(pl)),
           QString("Default"));
  QCOMPARE(tabs_->findPlaylistIndex("Default"), 0);
}

void TestPlaylistTabs::navigateIndex_selectsTargetRow() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-nav-a.mp3", "1"));
  pl->addSong(makeSong("B", "Artist", "/tmp/pt-nav-b.mp3", "2"));

  const MSong &song = pl->getSongByIndex(1);
  tabs_->navigateIndex(song, 1, pl);

  QTableView *table =
      tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);
  QCOMPARE(table->currentIndex().row(), 1);
  QVERIFY(table->selectionModel()->isRowSelected(1, QModelIndex()));
}

void TestPlaylistTabs::
    notifySongDataChangedInAllPlaylists_notifiesMatchingRows() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-notify-a.mp3", "1"));
  const int songPk = pl->getPkByIndex(0);

  QSignalSpy spy(pl, &QAbstractItemModel::dataChanged);

  tabs_->notifySongDataChangedInAllPlaylists(songPk);
  QCOMPARE(spy.count(), 1);

  tabs_->notifySongDataChangedInAllPlaylists(-1);
  QCOMPARE(spy.count(), 1);
}

void TestPlaylistTabs::customContextMenu_bindsRowIntoQueueActions() {
  if (QGuiApplication::primaryScreen() == nullptr) {
    QSKIP("No screen available for popup menu test");
  }

  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-menu-a.mp3", "1"));

  QTableView *table =
      tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);
  const QModelIndex row0 = table->model()->index(0, 0);
  QVERIFY(row0.isValid());
  const QPoint validPos = table->visualRect(row0).center();

  QTimer::singleShot(0, []() {
    if (QWidget *popup = QApplication::activePopupWidget()) {
      popup->close();
    }
  });
  tabs_->onCustomContextMenuRequested(validPos);
  QVERIFY(tabs_->playNextAction()->isEnabled());
  QVERIFY(tabs_->playEndAction()->isEnabled());
  QCOMPARE(tabs_->playNextAction()->data().value<QModelIndex>().row(), 0);
  QCOMPARE(tabs_->playEndAction()->data().value<QModelIndex>().row(), 0);

  QTimer::singleShot(0, []() {
    if (QWidget *popup = QApplication::activePopupWidget()) {
      popup->close();
    }
  });
  tabs_->onCustomContextMenuRequested(QPoint(-100, -100));
  QVERIFY(!tabs_->playNextAction()->isEnabled());
  QVERIFY(!tabs_->playEndAction()->isEnabled());
}

void TestPlaylistTabs::
    createNewPlaylistTabFromSongIds_createsTabAndCopiesSongs() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-search-a.mp3", "1"));
  pl->addSong(makeSong("B", "Artist", "/tmp/pt-search-b.mp3", "2"));

  const QList<int> songIds = {pl->getPkByIndex(0), pl->getPkByIndex(1)};
  tabs_->createNewPlaylistTabFromSongIds(songIds);

  QCOMPARE(tabs_->tabWidget()->count(), 2);
  QCOMPARE(tabs_->tabWidget()->currentIndex(), 1);
  Playlist *newPlaylist = tabs_->currentPlaylist();
  QVERIFY(newPlaylist != nullptr);
  QCOMPARE(newPlaylist->songCount(), 2);
  QCOMPARE(newPlaylist->getSongByIndex(0).at("title").text, std::string("A"));
  QCOMPARE(newPlaylist->getSongByIndex(1).at("title").text, std::string("B"));
}

void TestPlaylistTabs::createNewPlaylistTabFromSongIds_emptyNoop() {
  QCOMPARE(tabs_->tabWidget()->count(), 1);
  tabs_->createNewPlaylistTabFromSongIds({});
  QCOMPARE(tabs_->tabWidget()->count(), 1);
}

void TestPlaylistTabs::startupRestore_loadsAllPlaylistsInSavedOrder() {
  const int playlistA = insertPlaylistRow(databaseManager_->db(), "A", 1);
  const int playlistB = insertPlaylistRow(databaseManager_->db(), "B", 2);
  QVERIFY(playlistA > 0);
  QVERIFY(playlistB > 0);
  QVERIFY(updatePlaylistTabOrder(databaseManager_->db(),
                                 {1, playlistB, playlistA}));

  delete tabs_;
  tabs_ = new PlaylistTabs();
  tabs_->init(library_, queue_, manager_, playbackOrderGroup_, registry_,
              layout_, databaseManager_);

  QCOMPARE(tabs_->tabWidget()->count(), 3);
  QCOMPARE(tabs_->tabWidget()->tabText(0), QString("Default"));
  QCOMPARE(tabs_->tabWidget()->tabText(1), QString("B"));
  QCOMPARE(tabs_->tabWidget()->tabText(2), QString("A"));
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(0).toInt(), 1);
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(1).toInt(), playlistB);
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(2).toInt(), playlistA);
}

void TestPlaylistTabs::startupRestore_restoresLastOpenedPlaylistTab() {
  const int playlistA = insertPlaylistRow(databaseManager_->db(), "A", 1);
  const int playlistB = insertPlaylistRow(databaseManager_->db(), "B", 2);
  QVERIFY(playlistA > 0);
  QVERIFY(playlistB > 0);
  QVERIFY(updatePlaylistTabOrder(databaseManager_->db(),
                                 {1, playlistA, playlistB}));

  QSettings settings;
  settings.setValue("playlist/last_opened_id", playlistB);

  delete tabs_;
  tabs_ = new PlaylistTabs();
  tabs_->init(library_, queue_, manager_, playbackOrderGroup_, registry_,
              layout_, databaseManager_);

  QCOMPARE(tabs_->tabWidget()->currentIndex(), 2);
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(2).toInt(), playlistB);
}

void TestPlaylistTabs::startupRestore_recoversCorruptedTabOrder() {
  const int playlistA = insertPlaylistRow(databaseManager_->db(), "A", 1);
  const int playlistB = insertPlaylistRow(databaseManager_->db(), "B", 2);
  QVERIFY(playlistA > 0);
  QVERIFY(playlistB > 0);

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("UPDATE playlists SET tab_order=1 WHERE playlist_id=1"));
  QVERIFY(
      q.exec(QString("UPDATE playlists SET tab_order=2 WHERE playlist_id=%1")
                 .arg(playlistA)));
  QVERIFY(
      q.exec(QString("UPDATE playlists SET tab_order=3 WHERE playlist_id=%1")
                 .arg(playlistB)));

  delete tabs_;
  tabs_ = new PlaylistTabs();
  tabs_->init(library_, queue_, manager_, playbackOrderGroup_, registry_,
              layout_, databaseManager_);

  QCOMPARE(tabs_->tabWidget()->count(), 3);
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(0).toInt(), 1);
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(1).toInt(), playlistA);
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(2).toInt(), playlistB);

  QVERIFY(q.exec("SELECT tab_order FROM playlists ORDER BY tab_order ASC"));
  int expected = 0;
  while (q.next()) {
    QCOMPARE(q.value(0).toInt(), expected);
    expected += 1;
  }
  QCOMPARE(expected, 3);
}

void TestPlaylistTabs::removeTab_deletesNonDefaultPlaylist() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-remove-a.mp3", "1"));

  const QList<int> songIds = {pl->getPkByIndex(0)};
  tabs_->createNewPlaylistTabFromSongIds(songIds);
  QCOMPARE(tabs_->tabWidget()->count(), 2);

  const int removedPlaylistId =
      tabs_->tabWidget()->tabBar()->tabData(1).toInt();
  QVERIFY(removedPlaylistId > 1);

  QSqlQuery q(databaseManager_->db());
  QVERIFY(
      q.exec(QString("SELECT COUNT(*) FROM playlist_items WHERE playlist_id=%1")
                 .arg(removedPlaylistId)));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);

  QVERIFY(tabs_->removePlaylistTabByIndex(1));
  QCOMPARE(tabs_->tabWidget()->count(), 1);

  QVERIFY(q.exec(QString("SELECT COUNT(*) FROM playlists WHERE playlist_id=%1")
                     .arg(removedPlaylistId)));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);

  QVERIFY(
      q.exec(QString("SELECT COUNT(*) FROM playlist_items WHERE playlist_id=%1")
                 .arg(removedPlaylistId)));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

void TestPlaylistTabs::removeTab_rejectsWhenSingleTab() {
  QCOMPARE(tabs_->tabWidget()->count(), 1);
  QVERIFY(!tabs_->removePlaylistTabByIndex(0));
  QCOMPARE(tabs_->tabWidget()->count(), 1);
}

void TestPlaylistTabs::removeTab_invalidIndexRejected() {
  QCOMPARE(tabs_->tabWidget()->count(), 1);
  QVERIFY(!tabs_->removePlaylistTabByIndex(-1));
  QVERIFY(!tabs_->removePlaylistTabByIndex(99));
  QCOMPARE(tabs_->tabWidget()->count(), 1);
}

void TestPlaylistTabs::removeTab_allowsRemovingDefaultWhenMultipleTabs() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-remove-default-a.mp3", "1"));

  tabs_->createNewPlaylistTabFromSongIds({pl->getPkByIndex(0)});
  QCOMPARE(tabs_->tabWidget()->count(), 2);
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(0).toInt(), 1);

  QVERIFY(tabs_->removePlaylistTabByIndex(0));
  QCOMPARE(tabs_->tabWidget()->count(), 1);
  QVERIFY(tabs_->tabWidget()->tabBar()->tabData(0).toInt() != 1);

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("SELECT COUNT(*) FROM playlists WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

void TestPlaylistTabs::tabMove_persistsOrderInDb() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-move-a.mp3", "1"));
  pl->addSong(makeSong("B", "Artist", "/tmp/pt-move-b.mp3", "2"));

  tabs_->createNewPlaylistTabFromSongIds({pl->getPkByIndex(0)});
  tabs_->createNewPlaylistTabFromSongIds({pl->getPkByIndex(1)});
  QCOMPARE(tabs_->tabWidget()->count(), 3);

  const int idAt1 = tabs_->tabWidget()->tabBar()->tabData(1).toInt();
  const int idAt2 = tabs_->tabWidget()->tabBar()->tabData(2).toInt();
  QVERIFY(idAt1 > 1);
  QVERIFY(idAt2 > 1);

  tabs_->tabWidget()->tabBar()->moveTab(2, 1);

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec(QString("SELECT tab_order FROM playlists WHERE playlist_id=%1")
                     .arg(idAt1)));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 2);
  QVERIFY(q.exec(QString("SELECT tab_order FROM playlists WHERE playlist_id=%1")
                     .arg(idAt2)));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);
  QVERIFY(q.exec("SELECT tab_order FROM playlists WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

void TestPlaylistTabs::renameTab_updatesTabTextAndDb() {
  const int playlistId = insertPlaylistRow(databaseManager_->db(), "Old", 1);
  QVERIFY(playlistId > 0);

  delete tabs_;
  tabs_ = new PlaylistTabs();
  tabs_->init(library_, queue_, manager_, playbackOrderGroup_, registry_,
              layout_, databaseManager_);

  QCOMPARE(tabs_->tabWidget()->count(), 2);
  QCOMPARE(tabs_->tabWidget()->tabText(1), QString("Old"));

  QVERIFY(tabs_->renamePlaylistTabByIndex(1, "Renamed"));
  QCOMPARE(tabs_->tabWidget()->tabText(1), QString("Renamed"));

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec(QString("SELECT name FROM playlists WHERE playlist_id=%1")
                     .arg(playlistId)));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("Renamed"));
}

QTEST_MAIN(TestPlaylistTabs)
#include "tst_playlisttabs.moc"
