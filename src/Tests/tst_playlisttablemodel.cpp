#include <QMimeData>
#include <QObject>
#include <QPersistentModelIndex>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTest>
#include <QUuid>
#include <memory>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../globalcolumnlayoutmanager.h"
#include "../playbackqueue.h"
#include "../playlist.h"
#include "../playlisttablemodel.h"
#include "../songlibrary.h"
#include "../utils.h"

namespace {
MSong makeSong(const QString &title, const QString &artist, const QString &path,
               const QString &tracknumber = {}) {
  MSong song;
  song.insert_or_assign("title", FieldValue(title.toStdString(), "title"));
  song.insert_or_assign("artist", FieldValue(artist.toStdString(), "artist"));
  song.insert_or_assign("album", FieldValue("Album", "album"));
  song.insert_or_assign("discnumber", FieldValue("1", "discnumber"));
  if (!tracknumber.isEmpty()) {
    song.insert_or_assign("tracknumber",
                          FieldValue(tracknumber.toStdString(), "tracknumber"));
  }
  song.insert_or_assign("date", FieldValue("2024-01-01", "date"));
  song.insert_or_assign("genre", FieldValue("genre", "genre"));
  song.insert_or_assign("filepath", FieldValue(path.toStdString(), "filepath"));
  const std::string identity =
      util::normalizedText(title).toStdString() + "|" +
      util::normalizedText(artist).toStdString() + "|" +
      util::normalizedText(QStringLiteral("Album")).toStdString();
  song.insert_or_assign("song_identity_key",
                        FieldValue(identity, "song_identity_key"));
  return song;
}
} // namespace

class TestPlaylistTableModel : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void data_returnsSongFieldsAndPlayingStatus();
  void data_returnsQueuedPosition();
  void statusChanged_targetsStatusCellOfMatchingPlaylistOnly();
  void sortByColumnId_statusNoop_tracknumberSorts();
  void reorder_persistentIndexesFollowTheirSongs();
  void dropMimeData_movesDraggedRows();
  void songDataChanged_refreshesWholeRow();
  void playlistSignals_mapToRowInsertRemoveAndReset();
  void columnCount_tracksVisibilityChanges();
  void destroyedPlaylist_reportsNoRows();

private:
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  GlobalColumnLayoutManager *layout_ = nullptr;
  PlaybackQueue *queue_ = nullptr;
  QString connectionName_;
};

void TestPlaylistTableModel::init() {
  QCoreApplication::setOrganizationName("music-player-tests");
  QCoreApplication::setApplicationName("music-player-tests");
  QSettings settings;
  settings.clear();

  connectionName_ =
      QStringLiteral("test_playlisttablemodel_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
  layout_ = new GlobalColumnLayoutManager(*registry_);
  queue_ = new PlaybackQueue();
}

void TestPlaylistTableModel::cleanup() {
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

void TestPlaylistTableModel::data_returnsSongFieldsAndPlayingStatus() {
  Playlist playlist(-1, *library_, *databaseManager_);
  playlist.addSong(makeSong("First", "Artist", "/tmp/pl-first.mp3", "1"));
  playlist.addSong(makeSong("Second", "Artist", "/tmp/pl-second.mp3", "2"));

  PlaylistTableModel model(playlist, *queue_, *layout_);
  const QList<QString> visibleIds = layout_->visibleColumnIds();
  const int statusColumn = visibleIds.indexOf("status");
  const int titleColumn = visibleIds.indexOf("title");
  QVERIFY(statusColumn >= 0);
  QVERIFY(titleColumn >= 0);

  queue_->setCurrentId(playlist.getPkByIndex(0), &playlist);
  queue_->setPlaybackStatus(PlaybackQueue::PlaybackStatus::Playing);

  QCOMPARE(model.data(model.index(0, statusColumn), Qt::DisplayRole).toString(),
           QString::fromUtf8("▶"));
  QCOMPARE(model.data(model.index(0, titleColumn), Qt::DisplayRole).toString(),
           QString("First"));

  queue_->setPlaybackStatus(PlaybackQueue::PlaybackStatus::Paused);
  QCOMPARE(model.data(model.index(0, statusColumn), Qt::DisplayRole).toString(),
           QString::fromUtf8("⏸"));
}

void TestPlaylistTableModel::data_returnsQueuedPosition() {
  Playlist playlist(-1, *library_, *databaseManager_);
  playlist.addSong(makeSong("First", "Artist", "/tmp/pl-queued-1.mp3", "1"));
  playlist.addSong(makeSong("Second", "Artist", "/tmp/pl-queued-2.mp3", "2"));

  PlaylistTableModel model(playlist, *queue_, *layout_);
  const int statusColumn = layout_->visibleColumnIds().indexOf("status");
  QVERIFY(statusColumn >= 0);

  queue_->addLast(playlist.getPkByIndex(1), &playlist);

  QCOMPARE(model.data(model.index(1, statusColumn), Qt::DisplayRole).toString(),
           QString("1"));
  QCOMPARE(model.data(model.index(0, statusColumn), Qt::DisplayRole).toString(),
           QString{});
}

void TestPlaylistTableModel::
    statusChanged_targetsStatusCellOfMatchingPlaylistOnly() {
  Playlist playlist(-1, *library_, *databaseManager_);
  playlist.addSong(makeSong("First", "Artist", "/tmp/pl-status-1.mp3", "1"));
  Playlist other(-1, *library_, *databaseManager_);
  other.addSong(makeSong("Other", "Artist", "/tmp/pl-status-2.mp3", "2"));

  PlaylistTableModel model(playlist, *queue_, *layout_);
  const int statusColumn = layout_->visibleColumnIds().indexOf("status");
  QVERIFY(statusColumn >= 0);
  QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

  queue_->setCurrentId(other.getPkByIndex(0), &other);
  QCOMPARE(spy.count(), 0);

  queue_->setCurrentId(playlist.getPkByIndex(0), &playlist);
  QCOMPARE(spy.count(), 1);
  const QModelIndex topLeft = spy.at(0).at(0).value<QModelIndex>();
  QCOMPARE(topLeft.row(), 0);
  QCOMPARE(topLeft.column(), statusColumn);
}

void TestPlaylistTableModel::sortByColumnId_statusNoop_tracknumberSorts() {
  Playlist playlist(-1, *library_, *databaseManager_);
  playlist.addSong(makeSong("A", "Artist", "/tmp/pl-a.mp3", "10"));
  playlist.addSong(makeSong("B", "Artist", "/tmp/pl-b.mp3", "2"));
  playlist.addSong(makeSong("C", "Artist", "/tmp/pl-c.mp3", "1"));
  PlaylistTableModel model(playlist, *queue_, *layout_);
  QSignalSpy aboutSpy(&model, &QAbstractItemModel::layoutAboutToBeChanged);
  QSignalSpy spy(&model, &QAbstractItemModel::layoutChanged);

  const std::string before = playlist.getSongByIndex(0).at("title").text;
  model.sortByColumnId("status", 0);
  QCOMPARE(playlist.getSongByIndex(0).at("title").text, before);
  QCOMPARE(spy.count(), 0);

  model.sortByColumnId("tracknumber", 0);
  QCOMPARE(playlist.getSongByIndex(0).at("title").text, std::string("C"));
  QCOMPARE(playlist.getSongByIndex(1).at("title").text, std::string("B"));
  QCOMPARE(playlist.getSongByIndex(2).at("title").text, std::string("A"));
  QCOMPARE(aboutSpy.count(), 1);
  QCOMPARE(spy.count(), 1);
}

void TestPlaylistTableModel::reorder_persistentIndexesFollowTheirSongs() {
  Playlist playlist(-1, *library_, *databaseManager_);
  playlist.addSong(makeSong("A", "Artist", "/tmp/pl-follow-a.mp3", "3"));
  playlist.addSong(makeSong("B", "Artist", "/tmp/pl-follow-b.mp3", "2"));
  playlist.addSong(makeSong("C", "Artist", "/tmp/pl-follow-c.mp3", "1"));
  PlaylistTableModel model(playlist, *queue_, *layout_);
  const QPersistentModelIndex a(model.index(0, 1));
  const QPersistentModelIndex c(model.index(2, 0));

  // Any reorder (sortByField, moveRows) goes through the same signals.
  model.sortByColumnId("tracknumber", 0);
  QCOMPARE(a.row(), 2);
  QCOMPARE(a.column(), 1);
  QCOMPARE(c.row(), 0);
}

void TestPlaylistTableModel::dropMimeData_movesDraggedRows() {
  Playlist playlist(-1, *library_, *databaseManager_);
  playlist.addSong(makeSong("A", "Artist", "/tmp/pl-drag-a.mp3", "1"));
  playlist.addSong(makeSong("B", "Artist", "/tmp/pl-drag-b.mp3", "2"));
  playlist.addSong(makeSong("C", "Artist", "/tmp/pl-drag-c.mp3", "3"));
  playlist.addSong(makeSong("D", "Artist", "/tmp/pl-drag-d.mp3", "4"));
  PlaylistTableModel model(playlist, *queue_, *layout_);
  const auto titles = [&]() {
    QStringList out;
    for (int row = 0; row < playlist.songCount(); ++row) {
      out << QString::fromStdString(
          playlist.getSongByIndex(row).at("title").text);
    }
    return out;
  };

  QVERIFY(model.flags(model.index(0, 0)) & Qt::ItemIsDragEnabled);
  QVERIFY(!(model.flags(model.index(0, 0)) & Qt::ItemIsDropEnabled));
  QVERIFY(model.flags(QModelIndex()) & Qt::ItemIsDropEnabled);
  QCOMPARE(model.supportedDropActions(), Qt::DropActions(Qt::MoveAction));

  // Rows 0 and 2 (every column of each, as a row selection drags), dropped
  // on the empty viewport (no row, no parent): to the end.
  std::unique_ptr<QMimeData> data(
      model.mimeData({model.index(2, 0), model.index(0, 0), model.index(0, 1),
                      model.index(2, 1)}));
  QVERIFY(data);
  QVERIFY(model.canDropMimeData(data.get(), Qt::MoveAction, -1, -1, {}));
  QVERIFY(!model.canDropMimeData(data.get(), Qt::CopyAction, -1, -1, {}));
  QVERIFY(model.dropMimeData(data.get(), Qt::MoveAction, -1, -1, {}));
  QCOMPARE(titles(), (QStringList{"B", "D", "A", "C"}));

  // Dropped onto a row (row -1): in front of it.
  data.reset(model.mimeData({model.index(3, 0)}));
  QVERIFY(model.dropMimeData(data.get(), Qt::MoveAction, -1, -1,
                             model.index(0, 0)));
  QCOMPARE(titles(), (QStringList{"C", "B", "D", "A"}));

  // Dropped between rows (row 0). The drag carries songs, not rows: rows
  // changing before the drop (as an autoplaylist's can during the drag's
  // event loop) still move the dragged song, and a dragged song that left is
  // skipped.
  data.reset(model.mimeData({model.index(3, 0), model.index(0, 0)}));
  playlist.removeSongs({0});
  QCOMPARE(titles(), (QStringList{"B", "D", "A"}));
  QVERIFY(model.dropMimeData(data.get(), Qt::MoveAction, 0, 0, {}));
  QCOMPARE(titles(), (QStringList{"A", "B", "D"}));

  // A drop that changes nothing reports no move.
  data.reset(model.mimeData({model.index(0, 0)}));
  QVERIFY(!model.dropMimeData(data.get(), Qt::MoveAction, 1, 0, {}));
  QCOMPARE(titles(), (QStringList{"A", "B", "D"}));

  // Rows from another playlist's model are refused.
  Playlist other(-1, *library_, *databaseManager_);
  other.addSong(makeSong("X", "Artist", "/tmp/pl-drag-x.mp3", "1"));
  PlaylistTableModel otherModel(other, *queue_, *layout_);
  data.reset(otherModel.mimeData({otherModel.index(0, 0)}));
  QVERIFY(!model.canDropMimeData(data.get(), Qt::MoveAction, 0, 0, {}));
  QVERIFY(!model.dropMimeData(data.get(), Qt::MoveAction, 0, 0, {}));
  QCOMPARE(titles(), (QStringList{"A", "B", "D"}));
}

void TestPlaylistTableModel::songDataChanged_refreshesWholeRow() {
  Playlist playlist(-1, *library_, *databaseManager_);
  playlist.addSong(makeSong("A", "Artist", "/tmp/pl-row-a.mp3", "2"));
  playlist.addSong(makeSong("B", "Artist", "/tmp/pl-row-b.mp3", "1"));
  PlaylistTableModel model(playlist, *queue_, *layout_);
  const int songPk = playlist.getPkByIndex(0);
  model.sortByColumnId("tracknumber", 0);

  QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
  playlist.emitSongDataChangedBySongPk(songPk);

  QCOMPARE(spy.count(), 1);
  const QModelIndex topLeft = spy.at(0).at(0).value<QModelIndex>();
  const QModelIndex bottomRight = spy.at(0).at(1).value<QModelIndex>();
  QCOMPARE(topLeft.row(), 1);
  QCOMPARE(topLeft.column(), 0);
  QCOMPARE(bottomRight.row(), 1);
  QCOMPARE(bottomRight.column(), model.columnCount() - 1);
}

void TestPlaylistTableModel::playlistSignals_mapToRowInsertRemoveAndReset() {
  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [](const std::string &path, const ColumnRegistry &,
         std::unordered_map<std::string, std::string> *) -> MSong {
        return makeSong(QString::fromStdString(path), "Artist",
                        QString::fromStdString(path));
      });
  Playlist playlist(-1, localLibrary, *databaseManager_);
  PlaylistTableModel model(playlist, *queue_, *layout_);
  QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);
  QSignalSpy removedSpy(&model, &QAbstractItemModel::rowsRemoved);
  QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

  playlist.addFiles({"/tmp/pl-map-a.mp3", "/tmp/pl-map-b.mp3"});
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(insertedSpy.count(), 1);
  QCOMPARE(insertedSpy.at(0).at(1).toInt(), 0);
  QCOMPARE(insertedSpy.at(0).at(2).toInt(), 1);

  playlist.removeSongs({0});
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(removedSpy.count(), 1);
  QCOMPARE(removedSpy.at(0).at(1).toInt(), 0);
  QCOMPARE(removedSpy.at(0).at(2).toInt(), 0);

  playlist.clear();
  QCOMPARE(model.rowCount(), 0);
  QCOMPARE(resetSpy.count(), 1);
}

void TestPlaylistTableModel::columnCount_tracksVisibilityChanges() {
  Playlist playlist(-1, *library_, *databaseManager_);
  playlist.addSong(makeSong("A", "Artist", "/tmp/pl-cols-a.mp3", "1"));
  PlaylistTableModel model(playlist, *queue_, *layout_);

  QVERIFY(!layout_->isColumnVisible("genre"));
  const int before = model.columnCount();
  layout_->setColumnVisible("genre", true);
  QCOMPARE(model.columnCount(), before + 1);
}

void TestPlaylistTableModel::destroyedPlaylist_reportsNoRows() {
  auto *playlist = new Playlist(-1, *library_, *databaseManager_);
  playlist->addSong(makeSong("A", "Artist", "/tmp/pl-destroyed.mp3", "1"));
  PlaylistTableModel model(*playlist, *queue_, *layout_);
  QCOMPARE(model.rowCount(), 1);

  delete playlist;
  QCOMPARE(model.playlist(), nullptr);
  QCOMPARE(model.rowCount(), 0);
  QVERIFY(!model.data(model.index(0, 0), Qt::DisplayRole).isValid());
}

QTEST_MAIN(TestPlaylistTableModel)
#include "tst_playlisttablemodel.moc"
