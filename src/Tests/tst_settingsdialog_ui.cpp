#include <QObject>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
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
      {"attr:musicbrainz_trackid", "MusicBrainz Track ID",
       ColumnSource::SongAttribute, ColumnValueType::Text, true, true, 140}));
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
      {"attr:musicbrainz_trackid", "MusicBrainz Track ID",
       ColumnSource::SongAttribute, ColumnValueType::Text, true, true, 140}));
  QVERIFY(registry_->loadDynamicColumns(databaseManager_->db()));

  QSqlQuery seedValues(databaseManager_->db());
  QVERIFY(seedValues.exec(
      "INSERT INTO songs(song_id, title, filepath) VALUES (1, 'Song', '/tmp/remove-field.mp3')"));
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
  QVERIFY(q.exec(
      "SELECT COUNT(*) FROM attribute_definitions WHERE key='musicbrainz_trackid'"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);

  QVERIFY(q.exec(
      "SELECT COUNT(*) FROM song_attributes WHERE key='musicbrainz_trackid'"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

QTEST_MAIN(TestSettingsDialogUi)
#include "tst_settingsdialog_ui.moc"
