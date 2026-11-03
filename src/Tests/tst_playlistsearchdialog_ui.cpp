#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTableView>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../globalcolumnlayoutmanager.h"
#include "../monitoredlibrary.h"
#include "../playlistcollection.h"
#include "../playlistsearchdialog.h"
#include "../songlibrary.h"

namespace {
MSong makeSong(const QString &title, const QString &genre,
               const QString &path) {
  MSong song;
  song.insert_or_assign("title", FieldValue(title.toStdString(), "title"));
  song.insert_or_assign("artist", FieldValue("Artist", "artist"));
  song.insert_or_assign("album", FieldValue("Album", "album"));
  song.insert_or_assign("genre", FieldValue(genre.toStdString(), "genre"));
  song.insert_or_assign("filepath", FieldValue(path.toStdString(), "filepath"));
  return song;
}

// The row of the only emission, or -1.
int onlyRow(QSignalSpy &spy) {
  const int row = spy.size() == 1 ? spy.first().at(0).toInt() : -1;
  spy.clear();
  return row;
}
} // namespace

class TestPlaylistSearchDialogUi : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void query_showsModeCountAndMatchingRows();
  void results_followPlaylistRowsAndTarget();
  void results_followSongDataAndVisibleColumns();
  void actions_emitTheResultsCurrentRow();

private:
  int addSong(const QString &title, const QString &genre);
  void type(const QString &query);
  // The Title column of each result.
  QStringList resultTitles() const;
  template <typename T> T *child(const char *name) const {
    return dialog_->findChild<T *>(name);
  }

  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  MonitoredLibrary *monitored_ = nullptr;
  PlaylistCollection *collection_ = nullptr;
  GlobalColumnLayoutManager *layoutManager_ = nullptr;
  PlaylistSearchDialog *dialog_ = nullptr;
  Playlist *playlist_ = nullptr;
  int playlistId_ = -1;
  QString connectionName_;
};

void TestPlaylistSearchDialogUi::init() {
  QCoreApplication::setOrganizationName("music-player-tests");
  QCoreApplication::setApplicationName("playlistsearchdialog-tests");
  QSettings().clear();

  connectionName_ =
      QStringLiteral("test_playlistsearchdialog_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
  monitored_ = new MonitoredLibrary(*library_, *registry_, *databaseManager_);
  monitored_->load();
  collection_ =
      new PlaylistCollection(*library_, *databaseManager_, *monitored_);
  collection_->loadAll();
  layoutManager_ = new GlobalColumnLayoutManager(*registry_);

  playlistId_ = collection_->createPlaylist(
      "Jazz", {addSong("Blue in Green", "jazz"), addSong("Blue Train", "jazz"),
               addSong("So What", "modal")});
  playlist_ = collection_->playlist(playlistId_);
  dialog_ = new PlaylistSearchDialog(*library_, *collection_, *layoutManager_);
  dialog_->setPlaylist(playlist_);
}

void TestPlaylistSearchDialogUi::cleanup() {
  delete dialog_;
  dialog_ = nullptr;
  delete layoutManager_;
  layoutManager_ = nullptr;
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
}

int TestPlaylistSearchDialogUi::addSong(const QString &title,
                                        const QString &genre) {
  MSong song = makeSong(title, genre, "/music/" + title + ".mp3");
  library_->prepareParsedSong(song);
  return library_->addTolibrary(std::move(song));
}

void TestPlaylistSearchDialogUi::type(const QString &query) {
  auto *edit = child<QLineEdit>("playlist_search_query_edit");
  edit->clear();
  QTest::keyClicks(edit, query);
}

QStringList TestPlaylistSearchDialogUi::resultTitles() const {
  const QAbstractItemModel *model =
      child<QTableView>("playlist_search_results_table")->model();
  int titleColumn = -1;
  for (int col = 0; col < model->columnCount(); ++col) {
    if (model->headerData(col, Qt::Horizontal, Qt::DisplayRole) == "Title") {
      titleColumn = col;
    }
  }
  QStringList titles;
  for (int row = 0; row < model->rowCount(); ++row) {
    titles << model->index(row, titleColumn).data().toString();
  }
  return titles;
}

void TestPlaylistSearchDialogUi::query_showsModeCountAndMatchingRows() {
  auto *mode = child<QLabel>("playlist_search_mode_label");
  auto *status = child<QLabel>("playlist_search_status_label");
  QCOMPARE(dialog_->windowTitle(), QStringLiteral("Search Playlist: Jazz"));

  type("blue");
  QTRY_COMPARE(status->text(), QStringLiteral("2 matches"));
  QCOMPARE(mode->text(), QStringLiteral("Text"));
  QCOMPARE(resultTitles(), QStringList({"Blue in Green", "Blue Train"}));

  type("genre = modal");
  QTRY_COMPARE(status->text(), QStringLiteral("1 match"));
  QCOMPARE(mode->text(), QStringLiteral("Expression"));
  QCOMPARE(resultTitles(), QStringList({"So What"}));

  type("genre IN [pop");
  QTRY_VERIFY(status->text().contains("(at "));
  QCOMPARE(mode->text(), QStringLiteral("Expression"));
  QCOMPARE(resultTitles(), QStringList());

  type("  ");
  QTRY_COMPARE(status->text(), QString());
  QCOMPARE(mode->text(), QString());
  QCOMPARE(resultTitles(), QStringList());
}

void TestPlaylistSearchDialogUi::results_followPlaylistRowsAndTarget() {
  auto *status = child<QLabel>("playlist_search_status_label");
  type("blue");
  QTRY_COMPARE(status->text(), QStringLiteral("2 matches"));

  auto *table = child<QTableView>("playlist_search_results_table");
  table->selectRow(1);
  QVERIFY(playlist_->addSongByPk(addSong("Blue Monk", "jazz")));
  QTRY_COMPARE(status->text(), QStringLiteral("3 matches"));
  // The selected song stays selected.
  QCOMPARE(table->selectionModel()->selectedRows().value(0).row(), 1);
  // In the playlist's order.
  playlist_->sortByField("title", 1);
  QStringList sorted;
  for (int row = 0; row < playlist_->songCount(); ++row) {
    sorted << songFieldText(playlist_->getSongByIndex(row), "title");
  }
  sorted.removeAll("So What");
  QVERIFY(resultTitles() != sorted);
  QTRY_COMPARE(resultTitles(), sorted);

  QVERIFY(collection_->renamePlaylist(playlistId_, "Bop"));
  QCOMPARE(dialog_->windowTitle(), QStringLiteral("Search Playlist: Bop"));

  const int otherId =
      collection_->createPlaylist("Other", {addSong("Kind of Blue", "jazz")});
  dialog_->setPlaylist(collection_->playlist(otherId));
  QCOMPARE(dialog_->windowTitle(), QStringLiteral("Search Playlist: Other"));
  QCOMPARE(resultTitles(), QStringList({"Kind of Blue"}));

  dialog_->setPlaylist(nullptr);
  QCOMPARE(dialog_->windowTitle(), QStringLiteral("Search Playlist"));
  QCOMPARE(resultTitles(), QStringList());
}

void TestPlaylistSearchDialogUi::results_followSongDataAndVisibleColumns() {
  auto *status = child<QLabel>("playlist_search_status_label");
  type("play_count >= 1");
  QTRY_COMPARE(status->text(), QStringLiteral("0 matches"));
  const int pk = playlist_->getPkByIndex(1);
  QVERIFY(library_->incrementPlayCount(pk));
  collection_->notifySongChanged(pk);
  QTRY_COMPARE(resultTitles(), QStringList({"Blue Train"}));

  type("blue");
  QTRY_COMPARE(status->text(), QStringLiteral("2 matches"));
  playlist_->removeSongs({0});
  QTRY_COMPARE(resultTitles(), QStringList({"Blue Train"}));
  // Text is matched against the visible columns only.
  layoutManager_->setColumnVisible("title", false);
  QTRY_COMPARE(status->text(), QStringLiteral("0 matches"));
}

void TestPlaylistSearchDialogUi::actions_emitTheResultsCurrentRow() {
  auto *table = child<QTableView>("playlist_search_results_table");
  auto *show = child<QPushButton>("playlist_search_show_button");
  auto *queue = child<QPushButton>("playlist_search_queue_button");
  auto *properties = child<QPushButton>("playlist_search_properties_button");
  QSignalSpy playSpy(dialog_, &PlaylistSearchDialog::playRequested);
  QSignalSpy showSpy(dialog_, &PlaylistSearchDialog::showRequested);
  QSignalSpy queueSpy(dialog_, &PlaylistSearchDialog::queueRequested);
  QSignalSpy propertiesSpy(dialog_, &PlaylistSearchDialog::propertiesRequested);
  type("blue train");
  QTRY_COMPARE(resultTitles(), QStringList({"Blue Train"}));
  QVERIFY(!show->isEnabled());
  table->selectRow(0);
  QVERIFY(show->isEnabled() && queue->isEnabled() && properties->isEnabled());

  show->click();
  QCOMPARE(onlyRow(showSpy), 1);
  // The row is looked up when the action runs, before the results update.
  playlist_->removeSongs({0});
  queue->click();
  QCOMPARE(onlyRow(queueSpy), 0);
  properties->click();
  QCOMPARE(onlyRow(propertiesSpy), 0);
  QTest::keyClick(table, Qt::Key_Return);
  QCOMPARE(onlyRow(playSpy), 0);
  emit table->doubleClicked(table->model()->index(0, 0));
  QCOMPARE(onlyRow(playSpy), 0);

  // A result that left the playlist does nothing.
  playlist_->removeSongs({0});
  show->click();
  QCOMPARE(showSpy.count(), 0);
}

QTEST_MAIN(TestPlaylistSearchDialogUi)
#include "tst_playlistsearchdialog_ui.moc"
