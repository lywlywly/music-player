#include <QHeaderView>
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
#include "../librarysearchdialog.h"
#include "../songlibrary.h"
#include "../utils.h"

namespace {
MSong makeSong(const QString &title, const QString &artist, const QString &path,
               const QString &genre = {}) {
  MSong song;
  song["title"] = title.toStdString();
  song["artist"] = artist.toStdString();
  song["album"] = "Album";
  song["discnumber"] = FieldValue("1", ColumnValueType::Number);
  song["tracknumber"] = FieldValue("1", ColumnValueType::Number);
  song["date"] = FieldValue("2024-01-01", ColumnValueType::DateTime);
  if (!genre.isEmpty()) {
    song["genre"] = genre.toStdString();
  }
  song["filepath"] = path.toStdString();
  const std::string identity =
      util::normalizedText(title).toStdString() + "|" +
      util::normalizedText(artist).toStdString() + "|" +
      util::normalizedText(QStringLiteral("Album")).toStdString();
  song["song_identity_key"] = identity;
  return song;
}

int columnWithHeader(QTableView *table, const QString &title) {
  if (!table->model()) {
    return -1;
  }
  for (int col = 0; col < table->model()->columnCount(); ++col) {
    if (table->model()
            ->headerData(col, Qt::Horizontal, Qt::DisplayRole)
            .toString() == title) {
      return col;
    }
  }
  return -1;
}
} // namespace

class TestLibrarySearchDialogUi : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void validExpression_populatesReadOnlyResultsInLibraryOrder();
  void invalidExpression_showsInlineErrorAndClearsResults();
  void createPlaylistButton_emitsMatchingSongIds();

private:
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  GlobalColumnLayoutManager *layoutManager_ = nullptr;
  QString connectionName_;
};

void TestLibrarySearchDialogUi::init() {
  QCoreApplication::setOrganizationName("music-player-tests");
  QCoreApplication::setApplicationName("librarysearchdialog-tests");
  QSettings settings;
  settings.clear();

  connectionName_ =
      QStringLiteral("test_librarysearchdialog_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
  layoutManager_ = new GlobalColumnLayoutManager(*registry_);
}

void TestLibrarySearchDialogUi::cleanup() {
  delete layoutManager_;
  layoutManager_ = nullptr;
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
}

void TestLibrarySearchDialogUi::
    validExpression_populatesReadOnlyResultsInLibraryOrder() {
  library_->addTolibrary(
      makeSong("Song 1", "Artist A", "/tmp/library-search-a.mp3", "Pop"));
  library_->addTolibrary(
      makeSong("Song 2", "Artist B", "/tmp/library-search-b.mp3", "Jazz"));
  library_->addTolibrary(
      makeSong("Song 3", "Artist C", "/tmp/library-search-c.mp3", "Pop"));

  LibrarySearchDialog dialog(*library_, *layoutManager_);
  dialog.show();
  QTest::qWait(0);
  QLineEdit *expressionEdit = dialog.findChild<QLineEdit *>("expression_edit");
  QPushButton *searchButton = dialog.findChild<QPushButton *>("search_button");
  QLabel *errorLabel = dialog.findChild<QLabel *>("error_label");
  QTableView *resultsTable = dialog.findChild<QTableView *>("results_table");
  QVERIFY(expressionEdit != nullptr);
  QVERIFY(searchButton != nullptr);
  QVERIFY(errorLabel != nullptr);
  QVERIFY(resultsTable != nullptr);

  expressionEdit->setText("genre IS pop");
  QTest::mouseClick(searchButton, Qt::LeftButton);

  QAbstractItemModel *model = resultsTable->model();
  QVERIFY(model != nullptr);
  QCOMPARE(model->rowCount(), 2);
  QVERIFY(errorLabel->isVisible());
  QCOMPARE(errorLabel->text(), QString("2 matches"));

  const int titleColumn = columnWithHeader(resultsTable, "Title");
  QVERIFY(titleColumn >= 0);
  QCOMPARE(
      model->data(model->index(0, titleColumn), Qt::DisplayRole).toString(),
      QString("Song 1"));
  QCOMPARE(
      model->data(model->index(1, titleColumn), Qt::DisplayRole).toString(),
      QString("Song 3"));
  QVERIFY(!(model->flags(model->index(0, titleColumn)) & Qt::ItemIsEditable));
}

void TestLibrarySearchDialogUi::
    invalidExpression_showsInlineErrorAndClearsResults() {
  library_->addTolibrary(
      makeSong("Song 1", "Artist A", "/tmp/library-search-d.mp3", "Pop"));

  LibrarySearchDialog dialog(*library_, *layoutManager_);
  dialog.show();
  QTest::qWait(0);
  QLineEdit *expressionEdit = dialog.findChild<QLineEdit *>("expression_edit");
  QPushButton *searchButton = dialog.findChild<QPushButton *>("search_button");
  QLabel *errorLabel = dialog.findChild<QLabel *>("error_label");
  QTableView *resultsTable = dialog.findChild<QTableView *>("results_table");
  QVERIFY(expressionEdit != nullptr);
  QVERIFY(searchButton != nullptr);
  QVERIFY(errorLabel != nullptr);
  QVERIFY(resultsTable != nullptr);

  expressionEdit->setText("artist IS artist a");
  QTest::mouseClick(searchButton, Qt::LeftButton);
  QCOMPARE(resultsTable->model()->rowCount(), 1);

  expressionEdit->setText("artist AND");
  QTest::mouseClick(searchButton, Qt::LeftButton);

  QCOMPARE(resultsTable->model()->rowCount(), 0);
  QVERIFY(errorLabel->isVisible());
  QVERIFY(errorLabel->text().contains("Expected"));
  QVERIFY(errorLabel->text().contains("field name"));
}

void TestLibrarySearchDialogUi::createPlaylistButton_emitsMatchingSongIds() {
  library_->addTolibrary(
      makeSong("Song 1", "Artist A", "/tmp/library-search-e.mp3", "Pop"));
  library_->addTolibrary(
      makeSong("Song 2", "Artist B", "/tmp/library-search-f.mp3", "Jazz"));
  library_->addTolibrary(
      makeSong("Song 3", "Artist C", "/tmp/library-search-g.mp3", "Pop"));

  LibrarySearchDialog dialog(*library_, *layoutManager_);
  dialog.show();
  QTest::qWait(0);

  QSignalSpy createSpy(&dialog, &LibrarySearchDialog::createPlaylistRequested);
  QLineEdit *expressionEdit = dialog.findChild<QLineEdit *>("expression_edit");
  QPushButton *searchButton = dialog.findChild<QPushButton *>("search_button");
  QPushButton *createPlaylistButton =
      dialog.findChild<QPushButton *>("create_playlist_button");
  QVERIFY(expressionEdit != nullptr);
  QVERIFY(searchButton != nullptr);
  QVERIFY(createPlaylistButton != nullptr);
  QVERIFY(!createPlaylistButton->isEnabled());

  expressionEdit->setText("genre IS pop");
  QTest::mouseClick(searchButton, Qt::LeftButton);
  QVERIFY(createPlaylistButton->isEnabled());

  QTest::mouseClick(createPlaylistButton, Qt::LeftButton);
  QCOMPARE(createSpy.count(), 1);
  const QList<QVariant> args = createSpy.takeFirst();
  const QList<int> songIds = qvariant_cast<QList<int>>(args.at(0));
  QCOMPARE(songIds.size(), 2);
  QCOMPARE(library_->getSongByPK(songIds[0]).at("title").text,
           std::string("Song 1"));
  QCOMPARE(library_->getSongByPK(songIds[1]).at("title").text,
           std::string("Song 3"));
}

QTEST_MAIN(TestLibrarySearchDialogUi)
#include "tst_librarysearchdialog_ui.moc"
