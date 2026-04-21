#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "utils.h"
#include <QDebug>
#include <QMessageBox>
#include <QSettings>

SettingsDialog::SettingsDialog(ColumnRegistry &columnRegistry,
                               DatabaseManager &databaseManager,
                               QWidget *parent)
    : QDialog(parent), ui(new Ui::SettingsDialog),
      columnRegistry_(columnRegistry), databaseManager_(databaseManager) {
  ui->setupUi(this);

  QSettings settings;
  int backendIndex =
      settings
          .value(
              "playback/backend",
              static_cast<int>(PlaybackBackendManager::Backend::QMediaPlayer))
          .toInt();

  ui->backend_combo_box->setCurrentIndex(backendIndex);
  ui->custom_field_value_type_combo->addItem(
      "Text", static_cast<int>(ColumnValueType::Text));
  ui->custom_field_value_type_combo->addItem(
      "Number", static_cast<int>(ColumnValueType::Number));
  ui->custom_field_value_type_combo->addItem(
      "Date/Time", static_cast<int>(ColumnValueType::DateTime));
  ui->custom_field_visible_checkbox->setChecked(true);
  customFields_ = columnRegistry_.customTagDefinitions();
  for (const ColumnDefinition &definition : customFields_) {
    originalCustomFieldIds_.insert(definition.id);
  }

  refreshCustomFieldsList();

  connect(ui->add_custom_field_button, &QPushButton::clicked, this,
          &SettingsDialog::addCustomFieldFromForm);
  connect(ui->remove_custom_field_button, &QPushButton::clicked, this,
          &SettingsDialog::removeSelectedCustomField);
  connect(ui->custom_fields_list, &QListWidget::currentRowChanged, this,
          [this](int row) {
            ui->remove_custom_field_button->setEnabled(row >= 0);
          });
  connect(this, &QDialog::accepted, this, &SettingsDialog::applySettings);
}

SettingsDialog::~SettingsDialog() { delete ui; }

void SettingsDialog::refreshCustomFieldsList() {
  ui->custom_fields_list->clear();
  for (const ColumnDefinition &definition : customFields_) {
    const QString key = definition.id.mid(QStringLiteral("attr:").size());
    QListWidgetItem *item =
        new QListWidgetItem(QString("%1 (%2)").arg(definition.title, key));
    item->setData(Qt::UserRole, definition.id);
    ui->custom_fields_list->addItem(item);
  }
  ui->remove_custom_field_button->setEnabled(
      ui->custom_fields_list->currentRow() >= 0);
}

ColumnDefinition SettingsDialog::buildCustomFieldDefinitionFromForm() const {
  const QString displayName =
      ui->custom_field_display_name_edit->text().trimmed();
  const QString normalizedKey =
      util::canonicalizeTagKey(ui->custom_field_key_edit->text());
  const auto valueType = static_cast<ColumnValueType>(
      ui->custom_field_value_type_combo->currentData().toInt());

  return {QStringLiteral("attr:") + normalizedKey,
          displayName,
          ColumnSource::SongAttribute,
          valueType,
          true,
          ui->custom_field_visible_checkbox->isChecked(),
          140};
}

void SettingsDialog::addCustomFieldFromForm() {
  const ColumnDefinition definition = buildCustomFieldDefinitionFromForm();
  const QString key = definition.id.mid(QStringLiteral("attr:").size());

  if (definition.title.isEmpty()) {
    QMessageBox::warning(this, "Add Custom Field",
                         "Display name cannot be empty.");
    return;
  }
  if (key.isEmpty()) {
    QMessageBox::warning(this, "Add Custom Field", "Tag key cannot be empty.");
    return;
  }
  if (columnRegistry_.isBuiltInSongAttributeKey(key)) {
    QMessageBox::warning(this, "Add Custom Field",
                         "That tag key is already used by a built-in field.");
    return;
  }
  for (const ColumnDefinition &existing : customFields_) {
    if (existing.id == definition.id) {
      QMessageBox::warning(this, "Add Custom Field",
                           "That custom field already exists.");
      return;
    }
  }

  customFields_.push_back(definition);
  refreshCustomFieldsList();
  ui->custom_field_display_name_edit->clear();
  ui->custom_field_key_edit->clear();
  ui->custom_field_value_type_combo->setCurrentIndex(0);
  ui->custom_field_visible_checkbox->setChecked(true);
}

void SettingsDialog::removeSelectedCustomField() {
  QListWidgetItem *item = ui->custom_fields_list->currentItem();
  if (!item) {
    QMessageBox::warning(this, "Remove Custom Field",
                         "Select a custom field to remove.");
    return;
  }

  const QString columnId = item->data(Qt::UserRole).toString();
  for (auto it = customFields_.begin(); it != customFields_.end(); ++it) {
    if (it->id == columnId) {
      customFields_.erase(it);
      refreshCustomFieldsList();
      return;
    }
  }
}

void SettingsDialog::applySettings() {
  int selected = ui->backend_combo_box->currentIndex();
  qDebug() << "ui->backend_combo_box->currentIndex(): accepted:" << selected;
  auto selectedBackend = static_cast<PlaybackBackendManager::Backend>(selected);

  emit backendChanged(selectedBackend);

  QSettings settings;
  settings.setValue("playback/backend", selected);

  QSet<QString> currentCustomFieldIds;
  for (const ColumnDefinition &definition : customFields_) {
    currentCustomFieldIds.insert(definition.id);
    if (!columnRegistry_.upsertCustomTagDefinition(databaseManager_.db(),
                                                   definition)) {
      qFatal("applySettings: failed to persist custom field");
    }
  }

  for (const QString &columnId : originalCustomFieldIds_) {
    if (currentCustomFieldIds.contains(columnId)) {
      continue;
    }
    if (!columnRegistry_.removeCustomTagDefinition(databaseManager_.db(),
                                                   columnId)) {
      qFatal("applySettings: failed to remove custom field");
    }
  }

  if (currentCustomFieldIds != originalCustomFieldIds_) {
    emit customFieldsChanged();
  }
}
