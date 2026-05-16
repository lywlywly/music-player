#include "settingsdialog.h"
#include "displayexpressionevalcontext.h"
#include "libraryexpression_ops.h"
#include "libraryexpression_parser.h"
#include "statusruntimesymboltable.h"
#include "ui_settingsdialog.h"
#include "utils.h"
#include <QApplication>
#include <QColorDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSettings>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <unordered_map>

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
  const int defaultLyricsFontSize = QApplication::font().pointSize();
  const QString initialLyricsFontFamily =
      settings.value("lyrics/font_family", QString()).toString();
  const bool useSystemDefaultFont =
      settings
          .value("lyrics/use_system_default_font",
                 initialLyricsFontFamily.isEmpty())
          .toBool();
  const int initialLyricsFontSize =
      settings.value("lyrics/font_size", defaultLyricsFontSize).toInt();
  const QString defaultStatusExpression =
      StatusRuntimeSymbolTable::defaultStatusBarExpression();
  const QString initialStatusExpression =
      settings.value("status_bar/expression", defaultStatusExpression)
          .toString()
          .trimmed();
  const QString defaultWindowTitleExpression =
      StatusRuntimeSymbolTable::defaultWindowTitleExpression();
  const QString initialWindowTitleExpression =
      settings.value("window_title/expression", defaultWindowTitleExpression)
          .toString()
          .trimmed();
  const QString initialDisplayThemeMode =
      settings.value("display/theme_mode", QStringLiteral("system"))
          .toString()
          .trimmed()
          .toLower();
  lyricsHighlightColor_ = QColor(
      settings.value("lyrics/highlight_color", QString("#0064ff")).toString());
  if (!lyricsHighlightColor_.isValid()) {
    lyricsHighlightColor_ = QColor(0, 100, 255);
  }

  QWidget *lyricsTab = new QWidget(this);
  QVBoxLayout *lyricsLayout = new QVBoxLayout(lyricsTab);
  QLabel *lyricsNote =
      new QLabel("Change lyrics panel font family and size.", lyricsTab);
  lyricsNote->setWordWrap(true);
  lyricsLayout->addWidget(lyricsNote);
  QFormLayout *lyricsForm = new QFormLayout();
  lyricsFontFamilyCombo_ = new QFontComboBox(lyricsTab);
  lyricsFontSizeSpin_ = new QSpinBox(lyricsTab);
  lyricsUseDefaultFontCheck_ =
      new QCheckBox("Use system default font", lyricsTab);
  lyricsHighlightColorButton_ = new QPushButton(lyricsTab);
  lyricsFontSizeSpin_->setRange(8, 48);
  lyricsFontSizeSpin_->setValue(initialLyricsFontSize);

  const QString initialDisplayedFamily = initialLyricsFontFamily.trimmed();
  if (!initialDisplayedFamily.isEmpty()) {
    lyricsFontFamilyCombo_->setCurrentFont(QFont(initialDisplayedFamily));
  }
  if (lyricsFontFamilyCombo_->currentIndex() < 0 &&
      lyricsFontFamilyCombo_->count() > 0) {
    lyricsFontFamilyCombo_->setCurrentIndex(0);
  }
  lyricsUseDefaultFontCheck_->setChecked(useSystemDefaultFont);
  lyricsFontFamilyCombo_->setEnabled(!useSystemDefaultFont);
  connect(lyricsUseDefaultFontCheck_, &QCheckBox::toggled, this,
          [this](bool checked) {
            lyricsFontFamilyCombo_->setEnabled(!checked);
            if (!checked && lyricsFontFamilyCombo_->currentIndex() < 0 &&
                lyricsFontFamilyCombo_->count() > 0) {
              lyricsFontFamilyCombo_->setCurrentIndex(0);
            }
          });
  updateLyricsHighlightColorButton();
  connect(lyricsHighlightColorButton_, &QPushButton::clicked, this, [this]() {
    const QColor selected = QColorDialog::getColor(
        lyricsHighlightColor_, this, "Select lyrics highlight color");
    if (!selected.isValid()) {
      return;
    }
    lyricsHighlightColor_ = selected;
    updateLyricsHighlightColorButton();
  });
  lyricsForm->addRow("Font:", lyricsFontFamilyCombo_);
  lyricsForm->addRow("", lyricsUseDefaultFontCheck_);
  lyricsForm->addRow("Size:", lyricsFontSizeSpin_);
  lyricsForm->addRow("Highlight color:", lyricsHighlightColorButton_);
  lyricsLayout->addLayout(lyricsForm);
  lyricsLayout->addStretch();
  ui->settings_tab_widget->addTab(lyricsTab, "Lyrics");

  QWidget *statusTab = new QWidget(this);
  QVBoxLayout *statusLayout = new QVBoxLayout(statusTab);
  QLabel *statusNote = new QLabel(
      "Customize status bar and window title using the expression language.",
      statusTab);
  statusNote->setWordWrap(true);
  statusLayout->addWidget(statusNote);
  displayThemeModeCombo_ = new QComboBox(statusTab);
  displayThemeModeCombo_->setObjectName("display_theme_mode_combo");
  displayThemeModeCombo_->addItem("System default", "system");
  displayThemeModeCombo_->addItem("Light", "light");
  displayThemeModeCombo_->addItem("Dark", "dark");
  const int themeIndex =
      std::max(0, displayThemeModeCombo_->findData(initialDisplayThemeMode));
  displayThemeModeCombo_->setCurrentIndex(themeIndex);
  statusLayout->addWidget(new QLabel("Theme:", statusTab));
  statusLayout->addWidget(displayThemeModeCombo_);
  QLabel *statusExprLabel = new QLabel("Expression:", statusTab);
  statusLayout->addWidget(statusExprLabel);
  statusExpressionEdit_ = new QPlainTextEdit(statusTab);
  statusExpressionEdit_->setObjectName("status_expression_edit");
  statusExpressionEdit_->setMinimumHeight(140);
  statusExpressionEdit_->setSizePolicy(QSizePolicy::Expanding,
                                       QSizePolicy::Expanding);
  statusExpressionEdit_->setPlainText(initialStatusExpression.isEmpty()
                                          ? defaultStatusExpression
                                          : initialStatusExpression);
  statusExpressionEdit_->setPlaceholderText(defaultStatusExpression);
  statusLayout->addWidget(statusExpressionEdit_);
  QLabel *windowTitleExprLabel =
      new QLabel("Window title expression:", statusTab);
  statusLayout->addWidget(windowTitleExprLabel);
  windowTitleExpressionEdit_ = new QPlainTextEdit(statusTab);
  windowTitleExpressionEdit_->setObjectName("window_title_expression_edit");
  windowTitleExpressionEdit_->setMinimumHeight(80);
  windowTitleExpressionEdit_->setSizePolicy(QSizePolicy::Expanding,
                                            QSizePolicy::Expanding);
  windowTitleExpressionEdit_->setPlainText(
      initialWindowTitleExpression.isEmpty() ? defaultWindowTitleExpression
                                             : initialWindowTitleExpression);
  windowTitleExpressionEdit_->setPlaceholderText(defaultWindowTitleExpression);
  statusLayout->addWidget(windowTitleExpressionEdit_);
  QLabel *previewLabel = new QLabel("Preview (active expression):", statusTab);
  statusLayout->addWidget(previewLabel);
  activeDisplayExpressionPreviewLabel_ = new QLabel(statusTab);
  activeDisplayExpressionPreviewLabel_->setObjectName(
      "display_expression_preview_value_label");
  activeDisplayExpressionPreviewLabel_->setWordWrap(true);
  activeDisplayExpressionPreviewLabel_->setTextInteractionFlags(
      Qt::TextSelectableByMouse);
  statusLayout->addWidget(activeDisplayExpressionPreviewLabel_);
  resetDisplayExpressionsButton_ =
      new QPushButton("Reset to Default", statusTab);
  connect(resetDisplayExpressionsButton_, &QPushButton::clicked, this,
          [this]() {
            statusExpressionEdit_->setPlainText(
                StatusRuntimeSymbolTable::defaultStatusBarExpression());
            windowTitleExpressionEdit_->setPlainText(
                StatusRuntimeSymbolTable::defaultWindowTitleExpression());
          });
  const auto bindDisplayExpressionEditor = [this](QPlainTextEdit *editor) {
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this,
            [this, editor]() { setActiveDisplayExpressionEditor(editor); });
    connect(editor, &QPlainTextEdit::textChanged, this, [this, editor]() {
      if (activeDisplayExpressionEdit_ == editor) {
        updateActiveDisplayExpressionPreview();
      }
    });
  };
  bindDisplayExpressionEditor(statusExpressionEdit_);
  bindDisplayExpressionEditor(windowTitleExpressionEdit_);
  setActiveDisplayExpressionEditor(statusExpressionEdit_);
  statusLayout->addWidget(resetDisplayExpressionsButton_, 0, Qt::AlignLeft);
  statusLayout->addStretch();
  ui->settings_tab_widget->addTab(statusTab, "Display");
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
  ui->custom_field_value_type_combo->addItem("Text",
                                             static_cast<int>(ValueType::Text));
  ui->custom_field_value_type_combo->addItem(
      "Number", static_cast<int>(ValueType::Number));
  ui->custom_field_value_type_combo->addItem(
      "Date/Time", static_cast<int>(ValueType::DateTime));
  ui->custom_field_visible_checkbox->setChecked(true);

  ui->computed_field_value_type_combo->addItem(
      "Text", static_cast<int>(ValueType::Text));
  ui->computed_field_value_type_combo->addItem(
      "Number", static_cast<int>(ValueType::Number));
  ui->computed_field_value_type_combo->addItem(
      "Date/Time", static_cast<int>(ValueType::DateTime));
  ui->computed_field_value_type_combo->addItem(
      "Boolean", static_cast<int>(ValueType::Boolean));
  ui->computed_field_visible_checkbox->setChecked(true);

  customFields_ = columnRegistry_.customTagDefinitions();
  computedFields_ = columnRegistry_.computedDefinitions();
  for (const ColumnDefinition &definition : customFields_) {
    originalCustomFieldIds_.insert(definition.id);
    if (const FieldDefinition *field =
            columnRegistry_.findField(definition.id)) {
      customFieldSchemasById_.insert(definition.id, *field);
    }
  }
  for (const ColumnDefinition &definition : computedFields_) {
    originalComputedFieldIds_.insert(definition.id);
    if (const FieldDefinition *field =
            columnRegistry_.findField(definition.id)) {
      computedFieldSchemasById_.insert(definition.id, *field);
    }
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

void SettingsDialog::setDisplayExpressionPreviewContext(
    const std::unordered_map<std::string, FieldValue> *song,
    const StatusRuntimeSymbolTable &runtimeSymbols) {
  previewRuntimeSymbols_ = runtimeSymbols;
  previewSong_.clear();
  if (song) {
    previewSong_ = *song;
  }
  updateActiveDisplayExpressionPreview();
}

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
    const QString key = ColumnRegistry::computedKeyFromFieldId(definition.id);
    QListWidgetItem *item =
        new QListWidgetItem(QString("%1 (%2)").arg(definition.title, key));
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

  const QString columnId = QStringLiteral("attr:") + normalizedKey;
  return ColumnDefinition{
      .id = columnId,
      .title = displayName,
      .sortable = true,
      .visibleByDefault = ui->custom_field_visible_checkbox->isChecked(),
      .defaultWidth = 140,
  };
}

FieldDefinition SettingsDialog::buildCustomFieldSchemaFromForm() const {
  const QString normalizedKey =
      util::canonicalizeTagKey(ui->custom_field_key_edit->text());
  const auto valueType = static_cast<ValueType>(
      ui->custom_field_value_type_combo->currentData().toInt());
  const QString columnId = QStringLiteral("attr:") + normalizedKey;
  return FieldDefinition{.id = columnId,
                         .source = ColumnSource::SongAttribute,
                         .valueType = valueType,
                         .displayKind = DisplayKind::Raw,
                         .expression = QString{},
                         .searchable = true,
                         .writable = true};
}

ColumnDefinition SettingsDialog::buildComputedFieldDefinitionFromForm() const {
  const QString displayName =
      ui->computed_field_display_name_edit->text().trimmed();
  const QString key =
      util::canonicalizeTagKey(ui->computed_field_key_edit->text());
  return ColumnDefinition{
      .id = ColumnRegistry::computedFieldId(key),
      .title = displayName,
      .sortable = true,
      .visibleByDefault = ui->computed_field_visible_checkbox->isChecked(),
      .defaultWidth = 140,
  };
}

FieldDefinition SettingsDialog::buildComputedFieldSchemaFromForm() const {
  const QString normalizedKey =
      util::canonicalizeTagKey(ui->computed_field_key_edit->text());
  const auto valueType = static_cast<ValueType>(
      ui->computed_field_value_type_combo->currentData().toInt());
  const QString expression =
      ui->computed_field_expression_edit->text().trimmed();
  return FieldDefinition{.id = ColumnRegistry::computedFieldId(normalizedKey),
                         .source = ColumnSource::Computed,
                         .valueType = valueType,
                         .displayKind = DisplayKind::Raw,
                         .expression = expression,
                         .searchable = true,
                         .writable = false};
}

bool SettingsDialog::expressionTypeMatchesValueType(
    const Expr &expr, ValueType expectedType) const {
  const ExprStaticType actual = inferExprStaticType(expr);
  if (actual == ExprStaticType::Invalid) {
    return false;
  }

  if (expectedType == ValueType::Text || expectedType == ValueType::DateTime) {
    return actual == ExprStaticType::Text;
  }
  if (expectedType == ValueType::Number) {
    return actual == ExprStaticType::Number;
  }
  if (expectedType == ValueType::Boolean) {
    return actual == ExprStaticType::Bool;
  }
  return false;
}

void SettingsDialog::addCustomFieldFromForm() {
  const ColumnDefinition definition = buildCustomFieldDefinitionFromForm();
  const FieldDefinition field = buildCustomFieldSchemaFromForm();
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
  for (const ColumnDefinition &existing : customFields_) {
    if (existing.id == definition.id) {
      QMessageBox::warning(this, "Add Custom Field",
                           "That custom field already exists.");
      return;
    }
  }

  customFields_.push_back(definition);
  customFieldSchemasById_.insert(definition.id, field);
  refreshCustomFieldsList();
  ui->custom_field_display_name_edit->clear();
  ui->custom_field_key_edit->clear();
  ui->custom_field_value_type_combo->setCurrentIndex(0);
  ui->custom_field_visible_checkbox->setChecked(true);
}

void SettingsDialog::addComputedFieldFromForm() {
  const ColumnDefinition definition = buildComputedFieldDefinitionFromForm();
  const FieldDefinition field = buildComputedFieldSchemaFromForm();

  if (definition.title.isEmpty()) {
    QMessageBox::warning(this, "Add Computed Field",
                         "Display name cannot be empty.");
    return;
  }
  if (ColumnRegistry::computedKeyFromFieldId(definition.id).isEmpty()) {
    QMessageBox::warning(this, "Add Computed Field",
                         "Field key cannot be empty.");
    return;
  }
  if (field.expression.trimmed().isEmpty()) {
    QMessageBox::warning(this, "Add Computed Field",
                         "Expression cannot be empty.");
    return;
  }

  for (const ColumnDefinition &existing : computedFields_) {
    if (existing.id == definition.id) {
      QMessageBox::warning(this, "Add Computed Field",
                           "That computed field already exists.");
      return;
    }
  }

  if (columnRegistry_.isReservedComputedFieldKey(
          ColumnRegistry::computedKeyFromFieldId(definition.id))) {
    QMessageBox::warning(this, "Add Computed Field",
                         "That key conflicts with an existing field.");
    return;
  }

  const ExprSymbolResolver resolver(columnRegistry_.expressionSymbols());
  const ExprParseResult parsed =
      parseLibraryExpression(field.expression, resolver);
  if (!parsed.ok()) {
    QMessageBox::warning(
        this, "Add Computed Field",
        QString("Expression is invalid: %1").arg(parsed.error.message));
    return;
  }
  if (!expressionTypeMatchesValueType(*parsed.expr, field.valueType)) {
    QMessageBox::warning(
        this, "Add Computed Field",
        "Expression result type does not match selected value type.");
    return;
  }

  computedFields_.push_back(definition);
  computedFieldSchemasById_.insert(definition.id, field);
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
      customFieldSchemasById_.remove(columnId);
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
      computedFieldSchemasById_.remove(columnId);
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

void SettingsDialog::updateLyricsHighlightColorButton() {
  const QString colorHex = lyricsHighlightColor_.name(QColor::HexRgb);
  lyricsHighlightColorButton_->setText(colorHex);

  QPixmap swatch(20, 12);
  swatch.fill(Qt::transparent);
  QPainter painter(&swatch);
  painter.setPen(Qt::black);
  painter.setBrush(lyricsHighlightColor_);
  painter.drawRect(0, 0, swatch.width() - 1, swatch.height() - 1);
  painter.end();

  lyricsHighlightColorButton_->setIcon(QIcon(swatch));
  lyricsHighlightColorButton_->setIconSize(swatch.size());
}

void SettingsDialog::setActiveDisplayExpressionEditor(QPlainTextEdit *editor) {
  activeDisplayExpressionEdit_ = editor;
  updateActiveDisplayExpressionPreview();
}

QString SettingsDialog::evaluateDisplayExpressionPreview(
    const QString &expressionText, const QString &fallbackExpression) const {
  const QString expression =
      expressionText.trimmed().isEmpty() ? fallbackExpression : expressionText;
  const ExprSymbolResolver resolver(
      mergeExprSymbols(std::vector<ExprSymbolInfo>(
                           StatusRuntimeSymbolTable::expressionSymbols()),
                       columnRegistry_.expressionSymbols()));
  const ExprParseResult parsed = parseLibraryExpression(expression, resolver);
  if (!parsed.ok()) {
    return QStringLiteral("Invalid expression: %1").arg(parsed.error.message);
  }

  StatusRuntimeSymbolTable runtimeSymbols = previewRuntimeSymbols_;
  std::unordered_map<std::string, FieldValue> song = previewSong_;
  if (song.empty()) {
    runtimeSymbols.setIsPlaying(true);
    runtimeSymbols.setIsPaused(false);
    runtimeSymbols.setPlaybackTimeSeconds(65);
    runtimeSymbols.setDurationSeconds(125);
    runtimeSymbols.setBitrateKbps(320);

    song.insert_or_assign("artist", FieldValue("Artist", "artist"));
    song.insert_or_assign("title", FieldValue("Title", "title"));
    song.insert_or_assign("album", FieldValue("Album", "album"));
    song.insert_or_assign("codec", FieldValue("mp3", "codec"));
    song.insert_or_assign("sample_rate", FieldValue("44100", "sample_rate"));
    song.insert_or_assign("channels", FieldValue("2", "channels"));
  }

  DisplayExpressionEvalContext context(runtimeSymbols, &song);
  return runtimeValueToQString(parsed.expr->evaluateValue(context));
}

void SettingsDialog::updateActiveDisplayExpressionPreview() {
  if (!activeDisplayExpressionPreviewLabel_ || !activeDisplayExpressionEdit_) {
    return;
  }
  const bool isStatusEditor =
      activeDisplayExpressionEdit_ == statusExpressionEdit_;
  const QString fallback =
      isStatusEditor ? StatusRuntimeSymbolTable::defaultStatusBarExpression()
                     : StatusRuntimeSymbolTable::defaultWindowTitleExpression();
  activeDisplayExpressionPreviewLabel_->setText(
      evaluateDisplayExpressionPreview(
          activeDisplayExpressionEdit_->toPlainText(), fallback));
}

void SettingsDialog::applySettings() {
  int selected = ui->backend_combo_box->currentIndex();
  auto selectedBackend = static_cast<PlaybackBackendManager::Backend>(selected);

  emit backendChanged(selectedBackend);

  QSettings settings;
  settings.setValue("playback/backend", selected);

  const bool useSystemDefaultFont = lyricsUseDefaultFontCheck_->isChecked();
  if (lyricsFontFamilyCombo_->currentIndex() < 0 &&
      lyricsFontFamilyCombo_->count() > 0) {
    lyricsFontFamilyCombo_->setCurrentIndex(0);
  }
  QString resolvedFamily = lyricsFontFamilyCombo_->currentText().trimmed();
  if (resolvedFamily.isEmpty() && lyricsFontFamilyCombo_->count() > 0) {
    lyricsFontFamilyCombo_->setCurrentIndex(0);
    resolvedFamily = lyricsFontFamilyCombo_->currentText().trimmed();
  }
  settings.setValue("lyrics/use_system_default_font", useSystemDefaultFont);
  settings.setValue("lyrics/font_family", resolvedFamily);
  settings.setValue("lyrics/font_size", lyricsFontSizeSpin_->value());
  settings.setValue("lyrics/highlight_color",
                    lyricsHighlightColor_.name(QColor::HexRgb));
  QString statusExpression = statusExpressionEdit_->toPlainText().trimmed();
  if (statusExpression.isEmpty()) {
    statusExpression = StatusRuntimeSymbolTable::defaultStatusBarExpression();
  }
  QString windowTitleExpression =
      windowTitleExpressionEdit_->toPlainText().trimmed();
  if (windowTitleExpression.isEmpty()) {
    windowTitleExpression =
        StatusRuntimeSymbolTable::defaultWindowTitleExpression();
  }
  settings.setValue("status_bar/expression", statusExpression);
  settings.setValue("window_title/expression", windowTitleExpression);
  settings.setValue("display/theme_mode",
                    displayThemeModeCombo_->currentData().toString());
  const QString emittedFamily =
      useSystemDefaultFont ? QString() : resolvedFamily;
  emit lyricsFontChanged(emittedFamily, lyricsFontSizeSpin_->value());
  emit lyricsHighlightColorChanged(lyricsHighlightColor_);
  emit statusBarExpressionChanged(statusExpression);
  emit windowTitleExpressionChanged(windowTitleExpression);
  emit displayThemeModeChanged(
      displayThemeModeCombo_->currentData().toString());
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
    auto it = customFieldSchemasById_.find(definition.id);
    if (it == customFieldSchemasById_.end() ||
        !columnRegistry_.upsertCustomTagDefinition(db, definition,
                                                   it.value())) {
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
    auto it = computedFieldSchemasById_.find(definition.id);
    if (it == computedFieldSchemasById_.end() ||
        !columnRegistry_.upsertComputedDefinition(db, definition, it.value())) {
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
