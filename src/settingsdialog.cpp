#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "utils.h"
#include <QDebug>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QUuid>

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
  QString cloudUuid =
      settings.value("cloud_sync/user_uuid").toString().trimmed();
  const bool disabledByUser =
      settings.value("cloud_sync/disabled_by_user", false).toBool();
  if (cloudUuid.isEmpty() && !disabledByUser) {
    cloudUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  initialCloudUuid_ = cloudUuid;
  initialCloudDisabledByUser_ = disabledByUser;
  pendingCloudUuid_ = cloudUuid;
  pendingCloudDisabledByUser_ = disabledByUser;
  ui->cloud_uuid_edit->setText(pendingCloudUuid_);
  updateCloudUuidStatus();
  ui->custom_field_value_type_combo->addItem(
      "Text", static_cast<int>(ColumnValueType::Text));
  ui->custom_field_value_type_combo->addItem(
      "Number", static_cast<int>(ColumnValueType::Number));
  ui->custom_field_value_type_combo->addItem(
      "Date/Time", static_cast<int>(ColumnValueType::DateTime));
  ui->custom_field_visible_checkbox->setChecked(true);

  ui->computed_field_value_type_combo->addItem(
      "Text", static_cast<int>(ColumnValueType::Text));
  ui->computed_field_value_type_combo->addItem(
      "Number", static_cast<int>(ColumnValueType::Number));
  ui->computed_field_value_type_combo->addItem(
      "Date/Time", static_cast<int>(ColumnValueType::DateTime));
  ui->computed_field_value_type_combo->addItem(
      "Boolean", static_cast<int>(ColumnValueType::Boolean));
  ui->computed_field_visible_checkbox->setChecked(true);

  customFields_ = columnRegistry_.customTagDefinitions();
  computedFields_ = columnRegistry_.computedDefinitions();
  for (const ColumnDefinition &definition : customFields_) {
    originalCustomFieldIds_.insert(definition.id);
  }
  for (const ColumnDefinition &definition : computedFields_) {
    originalComputedFieldIds_.insert(definition.id);
  }

  refreshCustomFieldsList();
  refreshComputedFieldsList();

  connect(ui->add_custom_field_button, &QPushButton::clicked, this,
          &SettingsDialog::addCustomFieldFromForm);
  connect(ui->remove_custom_field_button, &QPushButton::clicked, this,
          &SettingsDialog::removeSelectedCustomField);
  connect(ui->custom_fields_list, &QListWidget::currentRowChanged, this,
          [this](int row) {
            ui->remove_custom_field_button->setEnabled(row >= 0);
          });

  connect(ui->add_computed_field_button, &QPushButton::clicked, this,
          &SettingsDialog::addComputedFieldFromForm);
  connect(ui->remove_computed_field_button, &QPushButton::clicked, this,
          &SettingsDialog::removeSelectedComputedField);
  connect(ui->computed_fields_list, &QListWidget::currentRowChanged, this,
          [this](int row) {
            ui->remove_computed_field_button->setEnabled(row >= 0);
          });

  connect(this, &QDialog::accepted, this, &SettingsDialog::applySettings);

  auto applyCloudUuid = [this](const QString &uuidText) {
    pendingCloudUuid_ = uuidText;
    pendingCloudDisabledByUser_ = uuidText.isEmpty();
    ui->cloud_uuid_edit->setText(pendingCloudUuid_);
    updateCloudUuidStatus();
  };

  connect(ui->set_cloud_uuid_button, &QPushButton::clicked, this,
          [this, applyCloudUuid]() {
            bool ok = false;
            const QString entered = QInputDialog::getText(
                this, "Set Cloud UUID", "User UUID:", QLineEdit::Normal,
                QString(), &ok);
            if (!ok) {
              return;
            }
            const QString uuidText = entered.trimmed();
            if (!uuidText.isEmpty() && QUuid(uuidText).isNull()) {
              QMessageBox::warning(this, "Set Cloud UUID",
                                   "Invalid UUID format.");
              return;
            }
            applyCloudUuid(uuidText);
            QMessageBox::information(
                this, "Set Cloud UUID",
                "Cloud UUID will be saved when you click OK. Keep it secret.");
          });
  connect(ui->start_new_cloud_user_button, &QPushButton::clicked, this,
          [this, applyCloudUuid]() {
            const QString uuidText =
                QUuid::createUuid().toString(QUuid::WithoutBraces);
            applyCloudUuid(uuidText);
            QMessageBox::information(
                this, "Start as New User",
                "A new cloud UUID was generated. It will be saved when you "
                "click OK.");
          });
  connect(ui->disable_cloud_sync_button, &QPushButton::clicked, this, [this]() {
    pendingCloudUuid_.clear();
    pendingCloudDisabledByUser_ = true;
    ui->cloud_uuid_edit->setText(pendingCloudUuid_);
    updateCloudUuidStatus();
    QMessageBox::information(
        this, "Disable Cloud Sync",
        "Cloud sync will be disabled when you click OK. Local play stats are "
        "kept.");
  });
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

void SettingsDialog::refreshComputedFieldsList() {
  ui->computed_fields_list->clear();
  for (const ColumnDefinition &definition : computedFields_) {
    QListWidgetItem *item = new QListWidgetItem(
        QString("%1 (%2)").arg(definition.title, definition.id));
    item->setData(Qt::UserRole, definition.id);
    ui->computed_fields_list->addItem(item);
  }
  ui->remove_computed_field_button->setEnabled(
      ui->computed_fields_list->currentRow() >= 0);
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
          "",
          true,
          ui->custom_field_visible_checkbox->isChecked(),
          140};
}

ColumnDefinition SettingsDialog::buildComputedFieldDefinitionFromForm() const {
  const QString displayName =
      ui->computed_field_display_name_edit->text().trimmed();
  const QString normalizedKey =
      util::canonicalizeTagKey(ui->computed_field_key_edit->text());
  const auto valueType = static_cast<ColumnValueType>(
      ui->computed_field_value_type_combo->currentData().toInt());
  const QString expression =
      ui->computed_field_expression_edit->text().trimmed();

  return {normalizedKey,
          displayName,
          ColumnSource::Computed,
          valueType,
          expression,
          true,
          ui->computed_field_visible_checkbox->isChecked(),
          140};
}

bool SettingsDialog::expressionTypeMatchesValueType(
    const Expr &expr, ColumnValueType expectedType) const {
  const ExprStaticType actual = inferExprStaticType(expr);
  if (actual == ExprStaticType::Invalid) {
    return false;
  }

  if (expectedType == ColumnValueType::Text ||
      expectedType == ColumnValueType::DateTime) {
    return actual == ExprStaticType::Text;
  }
  if (expectedType == ColumnValueType::Number) {
    return actual == ExprStaticType::Number;
  }
  if (expectedType == ColumnValueType::Boolean) {
    return actual == ExprStaticType::Bool;
  }
  return false;
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
  for (const ColumnDefinition &existing : computedFields_) {
    if (existing.id == key) {
      QMessageBox::warning(this, "Add Custom Field",
                           "That key is already used by a computed field.");
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

void SettingsDialog::addComputedFieldFromForm() {
  const ColumnDefinition definition = buildComputedFieldDefinitionFromForm();

  if (definition.title.isEmpty()) {
    QMessageBox::warning(this, "Add Computed Field",
                         "Display name cannot be empty.");
    return;
  }
  if (definition.id.isEmpty()) {
    QMessageBox::warning(this, "Add Computed Field",
                         "Field key cannot be empty.");
    return;
  }
  if (definition.expression.trimmed().isEmpty()) {
    QMessageBox::warning(this, "Add Computed Field",
                         "Expression cannot be empty.");
    return;
  }

  for (const ColumnDefinition &existing : customFields_) {
    if (existing.id == QStringLiteral("attr:") + definition.id) {
      QMessageBox::warning(this, "Add Computed Field",
                           "That key is already used by a custom tag field.");
      return;
    }
  }

  for (const ColumnDefinition &existing : computedFields_) {
    if (existing.id == definition.id) {
      QMessageBox::warning(this, "Add Computed Field",
                           "That computed field already exists.");
      return;
    }
  }

  if (columnRegistry_.isReservedComputedFieldKey(definition.id)) {
    QMessageBox::warning(this, "Add Computed Field",
                         "That key conflicts with an existing field.");
    return;
  }

  const ExprParseResult parsed =
      parseLibraryExpression(definition.expression, columnRegistry_);
  if (!parsed.ok()) {
    QMessageBox::warning(
        this, "Add Computed Field",
        QString("Expression is invalid: %1").arg(parsed.error.message));
    return;
  }
  if (!expressionTypeMatchesValueType(*parsed.expr, definition.valueType)) {
    QMessageBox::warning(
        this, "Add Computed Field",
        "Expression result type does not match selected value type.");
    return;
  }

  computedFields_.push_back(definition);
  refreshComputedFieldsList();
  ui->computed_field_display_name_edit->clear();
  ui->computed_field_key_edit->clear();
  ui->computed_field_expression_edit->clear();
  ui->computed_field_value_type_combo->setCurrentIndex(0);
  ui->computed_field_visible_checkbox->setChecked(true);
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

void SettingsDialog::removeSelectedComputedField() {
  QListWidgetItem *item = ui->computed_fields_list->currentItem();
  if (!item) {
    QMessageBox::warning(this, "Remove Computed Field",
                         "Select a computed field to remove.");
    return;
  }

  const QString columnId = item->data(Qt::UserRole).toString();
  for (auto it = computedFields_.begin(); it != computedFields_.end(); ++it) {
    if (it->id == columnId) {
      computedFields_.erase(it);
      refreshComputedFieldsList();
      return;
    }
  }
}

void SettingsDialog::updateCloudUuidStatus() {
  const QString uuidText = ui->cloud_uuid_edit->text().trimmed();
  if (uuidText.isEmpty()) {
    ui->cloud_uuid_status_label->setText("Disabled");
    ui->set_cloud_uuid_button->setVisible(true);
    ui->start_new_cloud_user_button->setVisible(true);
    ui->disable_cloud_sync_button->setVisible(false);
    return;
  }
  if (QUuid(uuidText).isNull()) {
    ui->cloud_uuid_status_label->setText("Invalid UUID");
    ui->set_cloud_uuid_button->setVisible(true);
    ui->start_new_cloud_user_button->setVisible(true);
    ui->disable_cloud_sync_button->setVisible(false);
    return;
  }
  ui->cloud_uuid_status_label->setText("Enabled");
  ui->set_cloud_uuid_button->setVisible(false);
  ui->start_new_cloud_user_button->setVisible(false);
  ui->disable_cloud_sync_button->setVisible(true);
}

void SettingsDialog::applySettings() {
  int selected = ui->backend_combo_box->currentIndex();
  auto selectedBackend = static_cast<PlaybackBackendManager::Backend>(selected);

  emit backendChanged(selectedBackend);

  QSettings settings;
  settings.setValue("playback/backend", selected);
  const QString cloudUuid = pendingCloudUuid_.trimmed();
  if (!cloudUuid.isEmpty() && QUuid(cloudUuid).isNull()) {
    QMessageBox::warning(this, "Settings", "Cloud UUID format is invalid.");
    return;
  }
  settings.setValue("cloud_sync/user_uuid", cloudUuid);
  settings.setValue("cloud_sync/disabled_by_user", pendingCloudDisabledByUser_);
  if (cloudUuid.isEmpty()) {
    settings.setValue("cloud_sync/rebase_pending", false);
  } else if (cloudUuid != initialCloudUuid_ ||
             pendingCloudDisabledByUser_ != initialCloudDisabledByUser_) {
    settings.setValue("cloud_sync/last_synced_at", 0);
    settings.setValue("cloud_sync/rebase_pending", true);
  }
  if (cloudUuid != initialCloudUuid_) {
    emit cloudUuidChanged(cloudUuid);
  }

  QSet<QString> currentCustomFieldIds;
  QSet<QString> currentComputedFieldIds;
  for (const ColumnDefinition &definition : customFields_) {
    currentCustomFieldIds.insert(definition.id);
  }
  for (const ColumnDefinition &definition : computedFields_) {
    currentComputedFieldIds.insert(definition.id);
  }

  QSqlDatabase &db = databaseManager_.db();
  if (!db.transaction()) {
    qFatal("applySettings: failed to start transaction");
  }

  for (const ColumnDefinition &definition : customFields_) {
    if (!columnRegistry_.upsertCustomTagDefinition(db, definition)) {
      db.rollback();
      qFatal("applySettings: failed to persist custom field");
    }
  }

  for (const QString &columnId : originalCustomFieldIds_) {
    if (currentCustomFieldIds.contains(columnId)) {
      continue;
    }
    if (!columnRegistry_.removeCustomTagDefinition(db, columnId)) {
      db.rollback();
      qFatal("applySettings: failed to remove custom field");
    }
  }

  for (const ColumnDefinition &definition : computedFields_) {
    if (!columnRegistry_.upsertComputedDefinition(db, definition)) {
      db.rollback();
      qFatal("applySettings: failed to persist computed field");
    }
  }

  for (const QString &columnId : originalComputedFieldIds_) {
    if (currentComputedFieldIds.contains(columnId)) {
      continue;
    }
    if (!columnRegistry_.removeComputedDefinition(db, columnId)) {
      db.rollback();
      qFatal("applySettings: failed to remove computed field");
    }
  }

  if (!db.commit()) {
    db.rollback();
    qFatal("applySettings: failed to commit transaction");
  }

  if (currentCustomFieldIds != originalCustomFieldIds_ ||
      currentComputedFieldIds != originalComputedFieldIds_) {
    emit customFieldsChanged();
  }
}
