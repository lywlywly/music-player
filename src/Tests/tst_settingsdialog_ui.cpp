#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../dummyfilesystemmonitor.h"
#include "../libraryfolderspage.h"
#include "../monitoredlibrary.h"
#include "../playbackbackendmanager.h"
#include "../settingsdialog.h"
#include "../songlibrary.h"

class TestSettingsDialogUi : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void showsExistingCustomFields();
  void addCustomField_persistsDefinitionAndEmitsSignal();
  void removeCustomField_deletesDefinitionAndValues();
  void addComputedField_persistsDefinitionAndEmitsSignal();
  void displayExpressions_persistAndEmitSignals();
  void displayExpressionPreview_tracksActiveEditor();
  void displayThemeMode_restoresPersistsAndEmitsSignal();
  void playbackBackend_restoresPersistsAndEmitsSignal();
  void libraryPage_addsRescansAndRemovesFolders();
  void libraryPage_marksOfflineFoldersAndRemovesMissingSongs();
  void libraryPage_marksFoldersWithoutLiveUpdates();
  void libraryPage_showsScanProgress();

private:
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  MonitoredLibrary *monitored_ = nullptr;
  QTemporaryDir *dir_ = nullptr;
  QString connectionName_;
};

void TestSettingsDialogUi::init() {
  QCoreApplication::setOrganizationName("music-player-tests");
  QCoreApplication::setApplicationName("settingsdialog-tests");
  QSettings settings;
  settings.clear();

  connectionName_ =
      QStringLiteral("test_settingsdialog_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
  // Any file is a song.
  monitored_ = new MonitoredLibrary(
      *library_, *registry_, *databaseManager_,
      [](const std::string &path, const ColumnRegistry &) {
        MSong song;
        song.insert_or_assign("title", FieldValue("Title", "title"));
        song.insert_or_assign("artist", FieldValue("Artist", "artist"));
        song.insert_or_assign("album", FieldValue("Album", "album"));
        song.insert_or_assign("filepath", FieldValue(path, "filepath"));
        return std::optional<MSong>(std::move(song));
      });
  monitored_->load();
  dir_ = new QTemporaryDir();
  QVERIFY(dir_->isValid());
}

void TestSettingsDialogUi::cleanup() {
  QTRY_VERIFY(!monitored_->isScanning());
  delete dir_;
  dir_ = nullptr;
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

void TestSettingsDialogUi::showsExistingCustomFields() {
  QVERIFY(registry_->upsertCustomTagDefinition(
      databaseManager_->db(),
      {.id = "attr:musicbrainz_trackid",
       .title = "MusicBrainz Track ID",
       .sortable = true,
       .visibleByDefault = true,
       .defaultWidth = 140},
      {.id = "attr:musicbrainz_trackid",
       .source = ColumnSource::SongAttribute,
       .valueType = ValueType::Text,
       .displayKind = DisplayKind::Raw,
       .expression = "",
       .searchable = true,
       .writable = true}));
  QVERIFY(registry_->loadDynamicColumns(databaseManager_->db()));

  SettingsDialog dialog(*registry_, *databaseManager_, *monitored_, {});
  QListWidget *list = dialog.findChild<QListWidget *>("custom_fields_list");
  QVERIFY(list != nullptr);
  QCOMPARE(list->count(), 1);
  QCOMPARE(list->item(0)->text(),
           QString("MusicBrainz Track ID (musicbrainz_trackid)"));
}

void TestSettingsDialogUi::addCustomField_persistsDefinitionAndEmitsSignal() {
  SettingsDialog dialog(*registry_, *databaseManager_, *monitored_, {});
  QSignalSpy customFieldsSpy(&dialog, &SettingsDialog::customFieldsChanged);

  QLineEdit *displayName =
      dialog.findChild<QLineEdit *>("custom_field_display_name_edit");
  QLineEdit *tagKey = dialog.findChild<QLineEdit *>("custom_field_key_edit");
  QComboBox *valueType =
      dialog.findChild<QComboBox *>("custom_field_value_type_combo");
  QCheckBox *visible =
      dialog.findChild<QCheckBox *>("custom_field_visible_checkbox");
  QPushButton *addButton =
      dialog.findChild<QPushButton *>("add_custom_field_button");
  QListWidget *list = dialog.findChild<QListWidget *>("custom_fields_list");
  QVERIFY(displayName != nullptr);
  QVERIFY(tagKey != nullptr);
  QVERIFY(valueType != nullptr);
  QVERIFY(visible != nullptr);
  QVERIFY(addButton != nullptr);
  QVERIFY(list != nullptr);

  displayName->setText("Original Year");
  tagKey->setText("Original Year");
  valueType->setCurrentIndex(2);
  visible->setChecked(false);
  QTest::mouseClick(addButton, Qt::LeftButton);

  QCOMPARE(list->count(), 1);
  QCOMPARE(list->item(0)->text(), QString("Original Year (original_year)"));

  dialog.accept();
  QCOMPARE(customFieldsSpy.count(), 1);

  QSqlQuery q(databaseManager_->db());
  q.prepare(R"(
      SELECT display_name, value_type, source, visible_default
      FROM attribute_definitions
      WHERE key=:key
  )");
  q.bindValue(":key", "original_year");
  QVERIFY(q.exec());
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("Original Year"));
  QCOMPARE(q.value(1).toString(), QString("date"));
  QCOMPARE(q.value(2).toString(), QString("custom_tag"));
  QCOMPARE(q.value(3).toInt(), 0);
}

void TestSettingsDialogUi::removeCustomField_deletesDefinitionAndValues() {
  QVERIFY(registry_->upsertCustomTagDefinition(
      databaseManager_->db(),
      {.id = "attr:musicbrainz_trackid",
       .title = "MusicBrainz Track ID",
       .sortable = true,
       .visibleByDefault = true,
       .defaultWidth = 140},
      {.id = "attr:musicbrainz_trackid",
       .source = ColumnSource::SongAttribute,
       .valueType = ValueType::Text,
       .displayKind = DisplayKind::Raw,
       .expression = "",
       .searchable = true,
       .writable = true}));
  QVERIFY(registry_->loadDynamicColumns(databaseManager_->db()));

  QSqlQuery seedValues(databaseManager_->db());
  QVERIFY(seedValues.exec(
      "INSERT INTO song_identities(identity_id, song_identity_key) VALUES "
      "(1, 'song|artist|album')"));
  QVERIFY(seedValues.exec(
      "INSERT INTO songs(song_id, title, filepath, identity_id) "
      "VALUES (1, 'Song', '/tmp/remove-field.mp3', 1)"));
  QVERIFY(seedValues.exec(
      "INSERT INTO song_attributes(song_id, key, value_text, value_type) "
      "VALUES (1, 'musicbrainz_trackid', 'abc123', 'text')"));

  SettingsDialog dialog(*registry_, *databaseManager_, *monitored_, {});
  QSignalSpy customFieldsSpy(&dialog, &SettingsDialog::customFieldsChanged);
  QListWidget *list = dialog.findChild<QListWidget *>("custom_fields_list");
  QPushButton *removeButton =
      dialog.findChild<QPushButton *>("remove_custom_field_button");
  QVERIFY(list != nullptr);
  QVERIFY(removeButton != nullptr);
  QCOMPARE(list->count(), 1);

  list->setCurrentRow(0);
  QVERIFY(removeButton->isEnabled());
  QTest::mouseClick(removeButton, Qt::LeftButton);
  QCOMPARE(list->count(), 0);

  dialog.accept();
  QCOMPARE(customFieldsSpy.count(), 1);

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("SELECT COUNT(*) FROM attribute_definitions WHERE "
                 "key='musicbrainz_trackid'"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);

  QVERIFY(q.exec(
      "SELECT COUNT(*) FROM song_attributes WHERE key='musicbrainz_trackid'"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

void TestSettingsDialogUi::addComputedField_persistsDefinitionAndEmitsSignal() {
  SettingsDialog dialog(*registry_, *databaseManager_, *monitored_, {});
  QSignalSpy customFieldsSpy(&dialog, &SettingsDialog::customFieldsChanged);

  QLineEdit *displayName =
      dialog.findChild<QLineEdit *>("computed_field_display_name_edit");
  QLineEdit *fieldKey =
      dialog.findChild<QLineEdit *>("computed_field_key_edit");
  QComboBox *valueType =
      dialog.findChild<QComboBox *>("computed_field_value_type_combo");
  QLineEdit *expression =
      dialog.findChild<QLineEdit *>("computed_field_expression_edit");
  QCheckBox *visible =
      dialog.findChild<QCheckBox *>("computed_field_visible_checkbox");
  QPushButton *addButton =
      dialog.findChild<QPushButton *>("add_computed_field_button");
  QListWidget *list = dialog.findChild<QListWidget *>("computed_fields_list");
  QVERIFY(displayName != nullptr);
  QVERIFY(fieldKey != nullptr);
  QVERIFY(valueType != nullptr);
  QVERIFY(expression != nullptr);
  QVERIFY(visible != nullptr);
  QVERIFY(addButton != nullptr);
  QVERIFY(list != nullptr);

  displayName->setText("Era");
  fieldKey->setText("Era");
  valueType->setCurrentIndex(0);
  expression->setText("IF date < 2000 THEN classic ELSE new");
  visible->setChecked(true);
  QTest::mouseClick(addButton, Qt::LeftButton);

  QCOMPARE(list->count(), 1);
  QCOMPARE(list->item(0)->text(), QString("Era (era)"));

  dialog.accept();
  QCOMPARE(customFieldsSpy.count(), 1);

  QSqlQuery q(databaseManager_->db());
  q.prepare(R"(
      SELECT display_name, value_type, expression, visible_default
      FROM computed_attribute_definitions
      WHERE key=:key
  )");
  q.bindValue(":key", "era");
  QVERIFY(q.exec());
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("Era"));
  QCOMPARE(q.value(1).toString(), QString("text"));
  QCOMPARE(q.value(2).toString(),
           QString("IF date < 2000 THEN classic ELSE new"));
  QCOMPARE(q.value(3).toInt(), 1);
}

void TestSettingsDialogUi::displayExpressions_persistAndEmitSignals() {
  SettingsDialog dialog(*registry_, *databaseManager_, *monitored_, {});
  QSignalSpy statusExprSpy(&dialog,
                           &SettingsDialog::statusBarExpressionChanged);
  QSignalSpy titleExprSpy(&dialog,
                          &SettingsDialog::windowTitleExpressionChanged);

  QPlainTextEdit *statusEdit =
      dialog.findChild<QPlainTextEdit *>("status_expression_edit");
  QPlainTextEdit *titleEdit =
      dialog.findChild<QPlainTextEdit *>("window_title_expression_edit");
  QVERIFY(statusEdit != nullptr);
  QVERIFY(titleEdit != nullptr);

  const QString statusExpr = "`${artist} | ${title}`";
  const QString titleExpr = "`${artist} - ${title}`";
  statusEdit->setPlainText(statusExpr);
  titleEdit->setPlainText(titleExpr);

  dialog.accept();

  QCOMPARE(statusExprSpy.count(), 1);
  QCOMPARE(titleExprSpy.count(), 1);
  QCOMPARE(statusExprSpy.at(0).at(0).toString(), statusExpr);
  QCOMPARE(titleExprSpy.at(0).at(0).toString(), titleExpr);

  QSettings settings;
  QCOMPARE(settings.value("status_bar/expression").toString(), statusExpr);
  QCOMPARE(settings.value("window_title/expression").toString(), titleExpr);
}

void TestSettingsDialogUi::displayExpressionPreview_tracksActiveEditor() {
  SettingsDialog dialog(*registry_, *databaseManager_, *monitored_, {});
  QPlainTextEdit *statusEdit =
      dialog.findChild<QPlainTextEdit *>("status_expression_edit");
  QPlainTextEdit *titleEdit =
      dialog.findChild<QPlainTextEdit *>("window_title_expression_edit");
  QLabel *preview =
      dialog.findChild<QLabel *>("display_expression_preview_value_label");
  QVERIFY(statusEdit != nullptr);
  QVERIFY(titleEdit != nullptr);
  QVERIFY(preview != nullptr);

  statusEdit->setPlainText("`${artist} - ${title}`");
  QTRY_COMPARE(preview->text(), QString("Artist - Title"));

  titleEdit->setFocus();
  QCoreApplication::processEvents();
  titleEdit->setPlainText("`${playback_time} / ${duration}`");
  QTRY_COMPARE(preview->text(), QString("01:05 / 02:05"));
}

void TestSettingsDialogUi::displayThemeMode_restoresPersistsAndEmitsSignal() {
  {
    QSettings settings;
    settings.setValue("display/theme_mode", " Light ");
  }
  SettingsDialog dialog(*registry_, *databaseManager_, *monitored_, {});
  QSignalSpy themeSpy(&dialog, &SettingsDialog::displayThemeModeChanged);

  QComboBox *themeCombo =
      dialog.findChild<QComboBox *>("display_theme_mode_combo");
  QVERIFY(themeCombo != nullptr);
  // The stored mode is trimmed and lowercased.
  QCOMPARE(themeCombo->currentData().toString(), QString("light"));
  const int darkIndex = themeCombo->findData("dark");
  QVERIFY(darkIndex >= 0);
  themeCombo->setCurrentIndex(darkIndex);

  dialog.accept();

  QCOMPARE(themeSpy.count(), 1);
  QCOMPARE(themeSpy.at(0).at(0).toString(), QString("dark"));
  QSettings settings;
  QCOMPARE(settings.value("display/theme_mode").toString(), QString("dark"));
}

void TestSettingsDialogUi::playbackBackend_restoresPersistsAndEmitsSignal() {
  {
    QSettings settings;
    settings.setValue(
        "playback/backend",
        static_cast<int>(PlaybackBackendManager::Backend::GStreamer));
  }
  SettingsDialog dialog(*registry_, *databaseManager_, *monitored_, {});
  QSignalSpy backendSpy(&dialog, &SettingsDialog::backendChanged);

  QComboBox *backendCombo = dialog.findChild<QComboBox *>("backend_combo_box");
  QVERIFY(backendCombo != nullptr);
  QCOMPARE(backendCombo->currentIndex(),
           static_cast<int>(PlaybackBackendManager::Backend::GStreamer));
  backendCombo->setCurrentIndex(
      static_cast<int>(PlaybackBackendManager::Backend::QMediaPlayer));

  dialog.accept();

  QCOMPARE(backendSpy.count(), 1);
  QCOMPARE(backendSpy.at(0).at(0).value<PlaybackBackendManager::Backend>(),
           PlaybackBackendManager::Backend::QMediaPlayer);
  QSettings settings;
  QCOMPARE(settings.value("playback/backend").toInt(),
           static_cast<int>(PlaybackBackendManager::Backend::QMediaPlayer));
}

void TestSettingsDialogUi::libraryPage_addsRescansAndRemovesFolders() {
  const QString root = QFileInfo(dir_->path()).canonicalFilePath();
  QVERIFY(QDir().mkpath(root + "/music/inner"));
  SettingsDialog dialog(*registry_, *databaseManager_, *monitored_, {});
  auto *page = dialog.findChild<LibraryFoldersPage *>();
  auto *list = dialog.findChild<QListWidget *>("library_folders_list");
  auto *removeButton =
      dialog.findChild<QPushButton *>("remove_library_folder_button");
  auto *rescanButton = dialog.findChild<QPushButton *>("rescan_library_button");
  QVERIFY(page != nullptr);
  QVERIFY(list != nullptr);
  QVERIFY(removeButton != nullptr);
  QVERIFY(rescanButton != nullptr);
  QCOMPARE(list->count(), 0);
  QVERIFY(!rescanButton->isEnabled());

  page->addFolder(root + "/music");
  QTRY_VERIFY(!monitored_->isScanning());
  QCOMPARE(list->count(), 1);
  QCOMPARE(list->item(0)->text(), QDir::toNativeSeparators(root + "/music"));
  QVERIFY(rescanButton->isEnabled());
  QVERIFY(!removeButton->isEnabled());

  // An overlapping folder is rejected with a warning.
  QString warning;
  QTimer::singleShot(0, [&warning]() {
    if (auto *box =
            qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
      warning = box->text();
      box->close();
    }
  });
  page->addFolder(root + "/music/inner");
  QVERIFY(!warning.isEmpty());
  QCOMPARE(list->count(), 1);

  QSignalSpy finishedSpy(monitored_, &MonitoredLibrary::scanFinished);
  QTest::mouseClick(rescanButton, Qt::LeftButton);
  QTRY_COMPARE(finishedSpy.count(), 1);

  list->setCurrentRow(0);
  QVERIFY(removeButton->isEnabled());
  QTest::mouseClick(removeButton, Qt::LeftButton);
  QCOMPARE(list->count(), 0);
  QVERIFY(monitored_->folders().empty());
  QVERIFY(!removeButton->isEnabled());
}

void TestSettingsDialogUi::
    libraryPage_marksOfflineFoldersAndRemovesMissingSongs() {
  const QString root = QFileInfo(dir_->path()).canonicalFilePath();
  const QString empty = root + "/empty";
  const QString missing = root + "/missing";
  for (const QString &folder : {empty, missing}) {
    QVERIFY(QDir().mkpath(folder));
    QFile file(folder + "/a.mp3");
    QVERIFY(file.open(QIODevice::WriteOnly));
  }
  SettingsDialog dialog(*registry_, *databaseManager_, *monitored_, {});
  auto *page = dialog.findChild<LibraryFoldersPage *>();
  auto *list = dialog.findChild<QListWidget *>("library_folders_list");
  auto *removeMissingButton =
      dialog.findChild<QPushButton *>("remove_missing_songs_button");
  QVERIFY(page != nullptr);
  QVERIFY(list != nullptr);
  QVERIFY(removeMissingButton != nullptr);
  page->addFolder(empty);
  page->addFolder(missing);
  QTRY_VERIFY(!monitored_->isScanning());
  QCOMPARE(monitored_->songPks().size(), size_t{2});

  // Offline because empty (case 2) and because missing (case 1).
  QVERIFY(QFile::remove(empty + "/a.mp3"));
  QVERIFY(QDir(missing).removeRecursively());
  monitored_->rescan();
  QTRY_VERIFY(!monitored_->isScanning());
  QCOMPARE(list->count(), 2);
  QCOMPARE(list->item(0)->text(),
           QDir::toNativeSeparators(empty) + " (Offline)");
  QCOMPARE(list->item(1)->text(),
           QDir::toNativeSeparators(missing) + " (Offline)");
  QCOMPARE(monitored_->songPks().size(), size_t{2});

  QVERIFY(!removeMissingButton->isEnabled());
  list->setCurrentRow(1);
  QVERIFY(!removeMissingButton->isEnabled());
  list->setCurrentRow(0);
  QVERIFY(removeMissingButton->isEnabled());

  QTest::mouseClick(removeMissingButton, Qt::LeftButton);
  QTRY_VERIFY(!monitored_->isScanning());
  QCOMPARE(monitored_->songPks().size(), size_t{1});
  QCOMPARE(list->item(0)->text(), QDir::toNativeSeparators(empty));
  QVERIFY(list->item(0)->isSelected());
  QVERIFY(!removeMissingButton->isEnabled());
}

void TestSettingsDialogUi::libraryPage_marksFoldersWithoutLiveUpdates() {
  const QString root = QFileInfo(dir_->path()).canonicalFilePath();
  DummyFileSystemMonitor monitor;
  monitor.refusedPaths = {root};
  MonitoredLibrary monitored(*library_, *registry_, *databaseManager_, {},
                             &monitor);
  SettingsDialog dialog(*registry_, *databaseManager_, monitored, {});
  auto *page = dialog.findChild<LibraryFoldersPage *>();
  auto *list = dialog.findChild<QListWidget *>("library_folders_list");
  QVERIFY(page != nullptr);
  QVERIFY(list != nullptr);

  page->addFolder(root);
  QCOMPARE(list->item(0)->text(),
           QDir::toNativeSeparators(root) + " (Live updates unavailable)");
  QTRY_VERIFY(!monitored.isScanning());
}

void TestSettingsDialogUi::libraryPage_showsScanProgress() {
  SettingsDialog dialog(*registry_, *databaseManager_, *monitored_, {});
  auto *label = dialog.findChild<QLabel *>("library_scan_status_label");
  QVERIFY(label != nullptr);
  QVERIFY(label->text().isEmpty());

  emit monitored_->scanProgress(3, 10);
  QCOMPARE(label->text(), QString("Scanning library... 3 / 10"));
  emit monitored_->scanFinished();
  QVERIFY(label->text().isEmpty());
}

QTEST_MAIN(TestSettingsDialogUi)
#include "tst_settingsdialog_ui.moc"
