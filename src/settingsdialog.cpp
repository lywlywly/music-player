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

void SettingsDialog::applySettings() {
  int selected = ui->backend_combo_box->currentIndex();
  qDebug() << "ui->backend_combo_box->currentIndex(): accepted:" << selected;
  auto selectedBackend = static_cast<PlaybackBackendManager::Backend>(selected);

  emit backendChanged(selectedBackend);

  QSettings settings;
  settings.setValue("playback/backend", selected);

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
