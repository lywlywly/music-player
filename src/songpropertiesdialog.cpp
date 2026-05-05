#include "songpropertiesdialog.h"
#include "addfielddialog.h"
#include "fieldeditdialog.h"
#include "songparser.h"
#include "ui_songpropertiesdialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <algorithm>
#include <unordered_map>

SongPropertiesDialog::SongPropertiesDialog(
    int songPk, const std::string &filepath, SongLibrary &songLibrary,
    const MSong &song,
    const std::unordered_map<std::string, std::string> &remainingFields,
    const ColumnRegistry &columnRegistry, QWidget *parent)
    : QDialog(parent), ui(new Ui::SongPropertiesDialog),
      songLibrary_(songLibrary), columnRegistry_(columnRegistry),
      songPk_(songPk), filepath_(filepath) {
  ui->setupUi(this);
  setModal(true);
  connect(ui->buttonBox, &QDialogButtonBox::accepted, this,
          &SongPropertiesDialog::saveBufferedChanges);
  connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(ui->properties_table, &QTableView::doubleClicked, this,
          &SongPropertiesDialog::handleTableDoubleClicked);
  connect(ui->add_field_button, &QPushButton::clicked, this,
          &SongPropertiesDialog::addField);
  connect(ui->remove_field_button, &QPushButton::clicked, this,
          &SongPropertiesDialog::removeSelectedField);
  buildRows(song, remainingFields);
  populateTable();
}

SongPropertiesDialog::~SongPropertiesDialog() { delete ui; }

bool SongPropertiesDialog::isNonTagInternalField(const std::string &key) {
  return key == "filepath" || key == "status" || key == "song_identity_key" ||
         key == "song_identity_id" || key == "play_count" ||
         key == "last_played_timestamp" || key == "date_added";
}

bool SongPropertiesDialog::isBuiltInFieldKey(const std::string &key) {
  static const std::unordered_set<std::string> kBuiltins = {
      "title",   "artist",      "album",      "date",     "genre",
      "comment", "tracknumber", "discnumber", "composer",
  };
  return kBuiltins.find(key) != kBuiltins.end();
}

QString
SongPropertiesDialog::valueTypeToDisplayText(ColumnValueType valueType) {
  switch (valueType) {
  case ColumnValueType::Text:
    return QStringLiteral("Text");
  case ColumnValueType::Number:
    return QStringLiteral("Number");
  case ColumnValueType::DateTime:
    return QStringLiteral("Date/Time");
  case ColumnValueType::Boolean:
    return QStringLiteral("Boolean");
  }
  return QStringLiteral("Text");
}

std::string SongPropertiesDialog::toWritableTagKey(const RowData &row) {
  if (row.rawKey.rfind("attr:", 0) == 0) {
    return row.rawKey.substr(5);
  }
  return row.rawKey;
}

void SongPropertiesDialog::showInfoPopup(QWidget *parent, const QString &title,
                                         const QString &message) {
  auto *notice = new QMessageBox(QMessageBox::Information, title, message,
                                 QMessageBox::Ok, parent);
  notice->setAttribute(Qt::WA_DeleteOnClose);
  notice->open();
}

bool SongPropertiesDialog::isRowRemovable(const RowData &row) const {
  if (row.source == RowSource::RemainingField) {
    return true;
  }
  if (row.source == RowSource::ComputedField || !row.editable) {
    return false;
  }
  return row.rawKey.rfind("attr:", 0) == 0 || !isBuiltInFieldKey(row.rawKey);
}

void SongPropertiesDialog::buildRows(
    const MSong &song,
    const std::unordered_map<std::string, std::string> &remainingFields) {
  rows_.clear();

  std::vector<std::string> songKeys;
  songKeys.reserve(song.size());
  for (const auto &[key, _] : song) {
    songKeys.push_back(key);
  }
  std::sort(songKeys.begin(), songKeys.end());
  for (const std::string &key : songKeys) {
    const auto &value = song.at(key);
    const QString qKey = QString::fromStdString(key);
    const ColumnDefinition *definition = columnRegistry_.findColumn(qKey);

    RowData row;
    row.rawKey = key;
    row.displayField =
        definition ? QStringLiteral("%1 (%2)").arg(definition->title, qKey)
                   : qKey;
    row.valueText = QString::fromStdString(value.text);
    row.originalValueText = row.valueText;
    row.valueType = definition ? definition->valueType : value.type;
    if (definition && definition->source == ColumnSource::Computed) {
      row.source = RowSource::ComputedField;
      row.editable = false;
    } else {
      row.source = RowSource::SongField;
      row.editable = !isNonTagInternalField(key);
    }
    rows_.push_back(std::move(row));
  }

  std::vector<std::string> remainingKeys;
  remainingKeys.reserve(remainingFields.size());
  for (const auto &[key, _] : remainingFields) {
    remainingKeys.push_back(key);
  }
  std::sort(remainingKeys.begin(), remainingKeys.end());
  for (const std::string &key : remainingKeys) {
    const auto &value = remainingFields.at(key);
    rows_.push_back(RowData{
        .rawKey = key,
        .displayField = QString::fromStdString(key),
        .valueText = QString::fromStdString(value),
        .originalValueText = QString::fromStdString(value),
        .valueType = ColumnValueType::Text,
        .source = RowSource::RemainingField,
        .editable = true,
        .dirty = false,
    });
  }
}

void SongPropertiesDialog::populateTable() {
  if (model_ == nullptr) {
    model_ = new QStandardItemModel(this);
    model_->setColumnCount(3);
    model_->setHorizontalHeaderLabels({QStringLiteral("Field"),
                                       QStringLiteral("Value"),
                                       QStringLiteral("Type")});
    ui->properties_table->setModel(model_);
  }
  model_->setRowCount(static_cast<int>(rows_.size()));

  for (int rowIndex = 0; rowIndex < static_cast<int>(rows_.size());
       ++rowIndex) {
    updateRowDisplay(rowIndex);
  }

  ui->properties_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui->properties_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  ui->properties_table->setSelectionMode(QAbstractItemView::SingleSelection);
  ui->properties_table->setSortingEnabled(false);
  ui->properties_table->verticalHeader()->setVisible(false);
  QHeaderView *header = ui->properties_table->horizontalHeader();
  header->setStretchLastSection(false);
  header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(1, QHeaderView::Interactive);
  header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  ui->properties_table->setColumnWidth(1, 420);
  updateRemoveButtonEnabled();
}

void SongPropertiesDialog::updateRowDisplay(int rowIndex) {
  const RowData &row = rows_[rowIndex];
  QString fieldText = row.displayField;
  if (row.dirty) {
    fieldText += QStringLiteral(" *");
  }
  model_->setItem(rowIndex, 0, new QStandardItem(fieldText));
  model_->setItem(rowIndex, 1, new QStandardItem(row.valueText));
  model_->setItem(rowIndex, 2,
                  new QStandardItem(valueTypeToDisplayText(row.valueType)));
}

void SongPropertiesDialog::handleTableDoubleClicked(const QModelIndex &index) {
  const int row = index.row();
  if (row < 0 || row >= static_cast<int>(rows_.size())) {
    return;
  }

  RowData &rowData = rows_[row];
  if (!rowData.editable) {
    QString message = QStringLiteral("This field cannot be edited.");
    if (rowData.source == RowSource::ComputedField) {
      message = QStringLiteral("Computed fields cannot be edited.");
    }
    showInfoPopup(this, QStringLiteral("Read-only field"), message);
    return;
  }

  FieldEditDialog dialog(rowData.displayField,
                         valueTypeToDisplayText(rowData.valueType),
                         rowData.valueText, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  rowData.valueText = dialog.editedValue();
  rowData.dirty = (rowData.valueText != rowData.originalValueText);
  updateRowDisplay(row);
}

void SongPropertiesDialog::updateRemoveButtonEnabled() {
  ui->remove_field_button->setEnabled(!rows_.empty());
}

void SongPropertiesDialog::addField() {
  AddFieldDialog dialog(this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  QString name = dialog.fieldName().trimmed();
  if (name.isEmpty()) {
    showInfoPopup(this, QStringLiteral("Invalid field name"),
                  QStringLiteral("Field name cannot be empty."));
    return;
  }
  if (name.startsWith(QStringLiteral("attr:"))) {
    showInfoPopup(this, QStringLiteral("Invalid field name"),
                  QStringLiteral("Use raw tag key without attr: prefix."));
    return;
  }
  const std::string rawKey = name.toStdString();

  for (const RowData &existing : rows_) {
    if (toWritableTagKey(existing) == rawKey) {
      showInfoPopup(this, QStringLiteral("Field exists"),
                    QStringLiteral("This field already exists."));
      return;
    }
  }

  RowData row{
      .rawKey = rawKey,
      .displayField = name,
      .valueText = dialog.fieldValue(),
      .originalValueText = QString(),
      .valueType = ColumnValueType::Text,
      .source = RowSource::RemainingField,
      .editable = true,
      .dirty = true,
  };
  rows_.push_back(std::move(row));
  removedTagKeys_.erase(rawKey);

  model_->setRowCount(static_cast<int>(rows_.size()));
  const int newRow = static_cast<int>(rows_.size()) - 1;
  updateRowDisplay(newRow);
  ui->properties_table->selectRow(newRow);
  updateRemoveButtonEnabled();
}

void SongPropertiesDialog::removeSelectedField() {
  const int row = ui->properties_table->currentIndex().row();
  if (row < 0 || row >= static_cast<int>(rows_.size())) {
    return;
  }
  if (!isRowRemovable(rows_[row])) {
    return;
  }

  removedTagKeys_.insert(toWritableTagKey(rows_[row]));
  rows_.erase(rows_.begin() + row);
  model_->removeRow(row);
  updateRemoveButtonEnabled();
}

void SongPropertiesDialog::saveBufferedChanges() {
  std::unordered_map<std::string, std::string> updatedFields;
  for (const RowData &row : rows_) {
    if (!row.editable || !row.dirty) {
      continue;
    }
    updatedFields[toWritableTagKey(row)] = row.valueText.toStdString();
  }
  for (const std::string &removedKey : removedTagKeys_) {
    updatedFields[removedKey] = "";
  }

  if (updatedFields.empty()) {
    accept();
    return;
  }

  if (!SongParser::writeTags(filepath_, updatedFields, columnRegistry_)) {
    showInfoPopup(this, QStringLiteral("Save failed"),
                  QStringLiteral("Failed to write tags to the file."));
    return;
  }

  std::unordered_map<std::string, std::string> remainingFields;
  const MSong &refreshedSong =
      songLibrary_.refreshSongFromFile(filepath_, &remainingFields);
  removedTagKeys_.clear();
  buildRows(refreshedSong, remainingFields);
  populateTable();
  emit songUpdated(songPk_);
  accept();
}
