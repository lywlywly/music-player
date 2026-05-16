#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../settingsdialog.h"

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
  void displayThemeMode_persistsAndEmitsSignal();

private:
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
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
}

void TestSettingsDialogUi::cleanup() {
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

  SettingsDialog dialog(*registry_, *databaseManager_);
  QListWidget *list = dialog.findChild<QListWidget *>("custom_fields_list");
  QVERIFY(list != nullptr);
  QCOMPARE(list->count(), 1);
  QCOMPARE(list->item(0)->text(),
           QString("MusicBrainz Track ID (musicbrainz_trackid)"));
}

void TestSettingsDialogUi::addCustomField_persistsDefinitionAndEmitsSignal() {
  SettingsDialog dialog(*registry_, *databaseManager_);
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

  SettingsDialog dialog(*registry_, *databaseManager_);
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
  SettingsDialog dialog(*registry_, *databaseManager_);
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
  SettingsDialog dialog(*registry_, *databaseManager_);
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
  SettingsDialog dialog(*registry_, *databaseManager_);
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

void TestSettingsDialogUi::displayThemeMode_persistsAndEmitsSignal() {
  SettingsDialog dialog(*registry_, *databaseManager_);
  QSignalSpy themeSpy(&dialog, &SettingsDialog::displayThemeModeChanged);

  QComboBox *themeCombo =
      dialog.findChild<QComboBox *>("display_theme_mode_combo");
  QVERIFY(themeCombo != nullptr);
  const int darkIndex = themeCombo->findData("dark");
  QVERIFY(darkIndex >= 0);
  themeCombo->setCurrentIndex(darkIndex);

  dialog.accept();

  QCOMPARE(themeSpy.count(), 1);
  QCOMPARE(themeSpy.at(0).at(0).toString(), QString("dark"));
  QSettings settings;
  QCOMPARE(settings.value("display/theme_mode").toString(), QString("dark"));
}

QTEST_MAIN(TestSettingsDialogUi)
#include "tst_settingsdialog_ui.moc"
