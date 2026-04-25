#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QObject>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
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
  return song;
}

QActionGroup *makePlaybackOrderGroup(QObject *parent = nullptr) {
  auto *group = new QActionGroup(parent);
  group->setExclusive(true);

  QAction *defaultAction = new QAction("Default", group);
  defaultAction->setCheckable(true);
  defaultAction->setChecked(true);
  group->addAction(defaultAction);

  QAction *shuffleAction = new QAction("Shuffle (tracks)", group);
  shuffleAction->setCheckable(true);
  group->addAction(shuffleAction);
  return group;
}
} // namespace

class TestPlaylistTabs : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void init_createsDefaultPlaylistAndActions();
  void helperMethods_mapAndFindPlaylist();
  void navigateIndex_selectsTargetRow();
  void notifySongDataChangedInAllPlaylists_notifiesMatchingRows();
  void customContextMenu_bindsRowIntoQueueActions();
  void createNewPlaylistTabFromSongIds_createsTabAndCopiesSongs();

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
              layout_);
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

void TestPlaylistTabs::helperMethods_mapAndFindPlaylist() {
  QCOMPARE(tabs_->string2Policy("Default"), Policy::Sequential);
  QCOMPARE(tabs_->string2Policy("Shuffle (tracks)"), Policy::Shuffle);
  QVERIFY_THROWS_EXCEPTION(std::logic_error, tabs_->string2Policy("invalid"));

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

  QSignalSpy spy(pl, &QAbstractItemModel::dataChanged);

  tabs_->notifySongDataChangedInAllPlaylists("/tmp/pt-notify-a.mp3");
  QCOMPARE(spy.count(), 1);

  tabs_->notifySongDataChangedInAllPlaylists("/tmp/pt-notify-miss.mp3");
  QCOMPARE(spy.count(), 1);

  tabs_->notifySongDataChangedInAllPlaylists("");
  QCOMPARE(spy.count(), 1);
}

void TestPlaylistTabs::customContextMenu_bindsRowIntoQueueActions() {
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

QTEST_MAIN(TestPlaylistTabs)
#include "tst_playlisttabs.moc"
