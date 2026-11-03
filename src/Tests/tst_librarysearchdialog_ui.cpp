#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../globalcolumnlayoutmanager.h"
#include "../librarysearchdialog.h"
#include "../monitoredlibrary.h"
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

  void validExpression_showsMonitoredMatchesInFilepathOrder();
  void invalidExpression_showsInlineErrorAndClearsResults();
  void createPlaylistButton_emitsMatchingSongIds();
  void saveAsAutoPlaylist_emitsSearchedExpression();
  void noWatchedFolders_showsHint();

private:
  // Writes the files under root(), watches root() and waits for the scan.
  void watchSongs(const QList<std::pair<QString, QByteArray>> &files);
  QString root() const;

  QTemporaryDir *dir_ = nullptr;
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  MonitoredLibrary *monitored_ = nullptr;
  GlobalColumnLayoutManager *layoutManager_ = nullptr;
  QString connectionName_;
};

void TestLibrarySearchDialogUi::init() {
  QCoreApplication::setOrganizationName("music-player-tests");
  QCoreApplication::setApplicationName("librarysearchdialog-tests");
  QSettings settings;
  settings.clear();

  dir_ = new QTemporaryDir();
  QVERIFY(dir_->isValid());
  connectionName_ =
      QStringLiteral("test_librarysearchdialog_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
  monitored_ =
      new MonitoredLibrary(*library_, *registry_, *databaseManager_,
                           [](const std::string &path, const ColumnRegistry &) {
                             return fakeParse(path);
                           });
  monitored_->load();
  layoutManager_ = new GlobalColumnLayoutManager(*registry_);
}

void TestLibrarySearchDialogUi::cleanup() {
  delete layoutManager_;
  layoutManager_ = nullptr;
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

QString TestLibrarySearchDialogUi::root() const {
  return QFileInfo(dir_->path()).canonicalFilePath() + "/music";
}

void TestLibrarySearchDialogUi::watchSongs(
    const QList<std::pair<QString, QByteArray>> &files) {
  for (const auto &[name, content] : files) {
    QVERIFY(writeFile(root() + "/" + name, content));
  }
  const MonitoredLibrary::AddFolderResult added = monitored_->addFolder(root());
  QVERIFY2(added.folderId > 0, qPrintable(added.error));
  QTRY_VERIFY_WITH_TIMEOUT(!monitored_->isScanning(), 5000);
}

void TestLibrarySearchDialogUi::
    validExpression_showsMonitoredMatchesInFilepathOrder() {
  watchSongs({{"a.mp3", "Song 1;Pop"},
              {"b.mp3", "Song 2;Jazz"},
              {"c.mp3", "Song 3;Pop"}});
  // A matching song only in the manual library is not searched.
  MSong manual;
  manual.insert_or_assign("title", FieldValue("Manual", "title"));
  manual.insert_or_assign("genre", FieldValue("Pop", "genre"));
  manual.insert_or_assign("artist", FieldValue("Artist", "artist"));
  manual.insert_or_assign("album", FieldValue("Album", "album"));
  manual.insert_or_assign(
      "filepath", FieldValue("/tmp/library-search-manual.mp3", "filepath"));
  library_->prepareParsedSong(manual);
  library_->addTolibrary(std::move(manual));

  LibrarySearchDialog dialog(*library_, *monitored_, *layoutManager_);
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
  QVERIFY(!errorLabel->isVisible());

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
  watchSongs({{"a.mp3", "Song 1;Pop"}});

  LibrarySearchDialog dialog(*library_, *monitored_, *layoutManager_);
  dialog.show();
  QTest::qWait(0);
  QLineEdit *expressionEdit = dialog.findChild<QLineEdit *>("expression_edit");
  QPushButton *searchButton = dialog.findChild<QPushButton *>("search_button");
  QPushButton *saveButton =
      dialog.findChild<QPushButton *>("save_autoplaylist_button");
  QLabel *errorLabel = dialog.findChild<QLabel *>("error_label");
  QTableView *resultsTable = dialog.findChild<QTableView *>("results_table");
  QVERIFY(expressionEdit != nullptr);
  QVERIFY(searchButton != nullptr);
  QVERIFY(saveButton != nullptr);
  QVERIFY(errorLabel != nullptr);
  QVERIFY(resultsTable != nullptr);

  expressionEdit->setText("title IS song 1");
  QTest::mouseClick(searchButton, Qt::LeftButton);
  QCOMPARE(resultsTable->model()->rowCount(), 1);
  QVERIFY(saveButton->isEnabled());

  expressionEdit->setText("artist AND");
  QTest::mouseClick(searchButton, Qt::LeftButton);

  QCOMPARE(resultsTable->model()->rowCount(), 0);
  QVERIFY(!saveButton->isEnabled());
  QVERIFY(errorLabel->isVisible());
  QVERIFY(errorLabel->text().contains("Expected"));
  QVERIFY(errorLabel->text().contains("field name"));

  // A bare word parses as text, not as a condition.
  expressionEdit->setText("davis");
  QTest::mouseClick(searchButton, Qt::LeftButton);
  QCOMPARE(resultsTable->model()->rowCount(), 0);
  QCOMPARE(errorLabel->text(), QString("Expression must be a condition"));
}

void TestLibrarySearchDialogUi::createPlaylistButton_emitsMatchingSongIds() {
  watchSongs({{"a.mp3", "Song 1;Pop"},
              {"b.mp3", "Song 2;Jazz"},
              {"c.mp3", "Song 3;Pop"}});

  LibrarySearchDialog dialog(*library_, *monitored_, *layoutManager_);
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

void TestLibrarySearchDialogUi::saveAsAutoPlaylist_emitsSearchedExpression() {
  watchSongs({{"a.mp3", "Song 1;Pop"}});

  LibrarySearchDialog dialog(*library_, *monitored_, *layoutManager_);
  dialog.show();
  QTest::qWait(0);
  QSignalSpy saveSpy(&dialog,
                     &LibrarySearchDialog::saveAsAutoPlaylistRequested);
  QLineEdit *expressionEdit = dialog.findChild<QLineEdit *>("expression_edit");
  QPushButton *searchButton = dialog.findChild<QPushButton *>("search_button");
  QPushButton *saveButton =
      dialog.findChild<QPushButton *>("save_autoplaylist_button");
  QVERIFY(expressionEdit != nullptr);
  QVERIFY(searchButton != nullptr);
  QVERIFY(saveButton != nullptr);
  QVERIFY(!saveButton->isEnabled());

  // No match is still a valid autoplaylist.
  expressionEdit->setText("genre IS rock");
  QTest::mouseClick(searchButton, Qt::LeftButton);
  QVERIFY(saveButton->isEnabled());
  // Saves what was searched, not the text edited since.
  expressionEdit->setText("genre IS");
  QTest::mouseClick(saveButton, Qt::LeftButton);

  QCOMPARE(saveSpy.count(), 1);
  QCOMPARE(saveSpy.at(0).at(0).toString(), QString("genre IS rock"));
  QVERIFY(!dialog.isVisible());
}

void TestLibrarySearchDialogUi::noWatchedFolders_showsHint() {
  LibrarySearchDialog dialog(*library_, *monitored_, *layoutManager_);
  dialog.show();
  QTest::qWait(0);
  QLineEdit *expressionEdit = dialog.findChild<QLineEdit *>("expression_edit");
  QPushButton *searchButton = dialog.findChild<QPushButton *>("search_button");
  QLabel *errorLabel = dialog.findChild<QLabel *>("error_label");
  QVERIFY(expressionEdit != nullptr);
  QVERIFY(searchButton != nullptr);
  QVERIFY(errorLabel != nullptr);
  const QString hint("No watched folders. Add one in Settings > Library.");
  QVERIFY(errorLabel->isVisible());
  QCOMPARE(errorLabel->text(), hint);

  expressionEdit->setText("true");
  QTest::mouseClick(searchButton, Qt::LeftButton);
  QCOMPARE(errorLabel->text(), hint);
}

QTEST_MAIN(TestLibrarySearchDialogUi)
#include "tst_librarysearchdialog_ui.moc"
