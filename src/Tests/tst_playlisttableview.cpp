#include <QHeaderView>
#include <QSettings>
#include <QShortcut>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../globalcolumnlayoutmanager.h"
#include "../playbackqueue.h"
#include "../playlist.h"
#include "../playlisttableview.h"
#include "../songlibrary.h"

namespace {
MSong makeSong(const std::string &title, const std::string &path) {
  MSong song;
  song.insert_or_assign("title", FieldValue(title, "title"));
  song.insert_or_assign("filepath", FieldValue(path, "filepath"));
  song.insert_or_assign("song_identity_key",
                        FieldValue(title, "song_identity_key"));
  return song;
}
} // namespace

class TestPlaylistTableView : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void layoutWidthChange_resizesColumnAndHidesSortIndicator();
  void columnResize_savesWidth();
  void columnMove_savesOrder();
  void sortableColumnClick_sortsPlaylist();
  void nonSortableColumnClick_doesNotSort();
  void propertiesShortcut_emitsFirstSelectedOrCurrentRow();

private:
  int column(const QString &id) const;

  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  GlobalColumnLayoutManager *layout_ = nullptr;
  PlaybackQueue *queue_ = nullptr;
  Playlist *playlist_ = nullptr;
  PlaylistTableView *view_ = nullptr;
  QString connectionName_;
};

void TestPlaylistTableView::init() {
  QCoreApplication::setOrganizationName("music-player-tests");
  QCoreApplication::setApplicationName("playlisttableview-tests");
  QSettings settings;
  settings.clear();

  connectionName_ =
      QStringLiteral("test_playlisttableview_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
  layout_ = new GlobalColumnLayoutManager(*registry_);
  queue_ = new PlaybackQueue();
  playlist_ = new Playlist(-1, *library_, *databaseManager_);
  playlist_->addSong(makeSong("B", "/tmp/ptv-b.mp3"));
  playlist_->addSong(makeSong("C", "/tmp/ptv-c.mp3"));
  playlist_->addSong(makeSong("A", "/tmp/ptv-a.mp3"));
  view_ = new PlaylistTableView(*playlist_, *queue_, *registry_, *layout_);
}

void TestPlaylistTableView::cleanup() {
  delete view_;
  view_ = nullptr;
  delete playlist_;
  playlist_ = nullptr;
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

int TestPlaylistTableView::column(const QString &id) const {
  return layout_->visibleColumnIds().indexOf(id);
}

void TestPlaylistTableView::
    layoutWidthChange_resizesColumnAndHidesSortIndicator() {
  view_->horizontalHeader()->setSortIndicatorShown(true);

  layout_->setColumnWidth("title", 333);

  QCOMPARE(view_->columnWidth(column("title")), 333);
  QVERIFY(!view_->horizontalHeader()->isSortIndicatorShown());
}

void TestPlaylistTableView::columnResize_savesWidth() {
  view_->horizontalHeader()->resizeSection(column("title"), 250);

  QCOMPARE(layout_->columnWidth("title"), 250);
}

void TestPlaylistTableView::columnMove_savesOrder() {
  // Hidden columns in a non-default order, which the move keeps.
  QList<QString> hidden;
  for (const QString &id : layout_->allOrderedColumnIds()) {
    if (!layout_->isColumnVisible(id)) {
      hidden.prepend(id);
    }
  }
  layout_->setOrder(layout_->visibleColumnIds() + hidden);
  QList<QString> expectedVisible = layout_->visibleColumnIds();
  expectedVisible.move(column("title"), 0);

  view_->horizontalHeader()->moveSection(column("title"), 0);

  QCOMPARE(layout_->visibleColumnIds(), expectedVisible);
  QCOMPARE(layout_->allOrderedColumnIds(), expectedVisible + hidden);
}

void TestPlaylistTableView::sortableColumnClick_sortsPlaylist() {
  QHeaderView *header = view_->horizontalHeader();
  header->setSortIndicator(column("title"), Qt::AscendingOrder);

  emit header->sectionClicked(column("title"));

  QVERIFY(header->isSortIndicatorShown());
  QCOMPARE(playlist_->getSongByIndex(0).at("title").text, std::string("A"));
  QCOMPARE(playlist_->getSongByIndex(1).at("title").text, std::string("B"));
  QCOMPARE(playlist_->getSongByIndex(2).at("title").text, std::string("C"));
}

void TestPlaylistTableView::nonSortableColumnClick_doesNotSort() {
  QHeaderView *header = view_->horizontalHeader();
  header->setSortIndicatorShown(true);

  emit header->sectionClicked(column("status"));

  QVERIFY(!header->isSortIndicatorShown());
  QCOMPARE(playlist_->getSongByIndex(0).at("title").text, std::string("B"));
}

void TestPlaylistTableView::
    propertiesShortcut_emitsFirstSelectedOrCurrentRow() {
  const QList<QShortcut *> shortcuts = view_->findChildren<QShortcut *>();
  QList<QKeySequence> keys;
  for (QShortcut *shortcut : shortcuts) {
    keys.push_back(shortcut->key());
    QCOMPARE(shortcut->context(), Qt::WidgetWithChildrenShortcut);
  }
  QCOMPARE(keys, PlaylistTableView::propertiesShortcuts());

  QSignalSpy spy(view_, &PlaylistTableView::propertiesRequested);
  QItemSelectionModel *selection = view_->selectionModel();
  selection->select(view_->model()->index(2, 0),
                    QItemSelectionModel::Select | QItemSelectionModel::Rows);
  selection->select(view_->model()->index(1, 0),
                    QItemSelectionModel::Select | QItemSelectionModel::Rows);
  emit shortcuts.front()->activated();
  QCOMPARE(spy.takeFirst().at(0).toInt(), 1);

  selection->clearSelection();
  selection->setCurrentIndex(view_->model()->index(2, 0),
                             QItemSelectionModel::NoUpdate);
  emit shortcuts.front()->activated();
  QCOMPARE(spy.takeFirst().at(0).toInt(), 2);
}

QTEST_MAIN(TestPlaylistTableView)
#include "tst_playlisttableview.moc"
