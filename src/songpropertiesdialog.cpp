#include "songpropertiesdialog.h"
#include "ui_songpropertiesdialog.h"

#include <QHeaderView>
#include <QStandardItemModel>
#include <algorithm>

SongPropertiesDialog::SongPropertiesDialog(
    const MSong &song,
    const std::unordered_map<std::string, std::string> &remainingFields,
    const ColumnRegistry &columnRegistry, QWidget *parent)
    : QDialog(parent), ui(new Ui::SongPropertiesDialog) {
  ui->setupUi(this);
  setModal(true);
  buildRows(song, remainingFields, columnRegistry);
  populateTable();
}

SongPropertiesDialog::~SongPropertiesDialog() { delete ui; }

SongPropertiesDialog::RowSource
SongPropertiesDialog::rowSourceAt(int row) const {
  if (row < 0 || row >= static_cast<int>(rows_.size())) {
    return RowSource::SongField;
  }
  return rows_[row].source;
}

int SongPropertiesDialog::propertyRowCount() const {
  return static_cast<int>(rows_.size());
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

void SongPropertiesDialog::buildRows(
    const MSong &song,
    const std::unordered_map<std::string, std::string> &remainingFields,
    const ColumnRegistry &columnRegistry) {
  rows_.clear();

  std::vector<std::string> songKeys;
  songKeys.reserve(song.size());
  for (const auto &[key, _] : song) {
    songKeys.push_back(key);
  }
  std::sort(songKeys.begin(), songKeys.end());
  for (const std::string &key : songKeys) {
    auto it = song.find(key);
    if (it == song.end()) {
      continue;
    }
    const QString qKey = QString::fromStdString(key);
    const ColumnDefinition *definition = columnRegistry.findColumn(qKey);

    RowData row;
    row.rawKey = key;
    row.displayField =
        definition ? QStringLiteral("%1 (%2)").arg(definition->title, qKey)
                   : qKey;
    row.valueText = QString::fromStdString(it->second.text);
    row.valueType = definition ? definition->valueType : it->second.type;
    if (definition && definition->source == ColumnSource::Computed) {
      row.source = RowSource::ComputedField;
    } else {
      row.source = RowSource::SongField;
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
    auto it = remainingFields.find(key);
    if (it == remainingFields.end()) {
      continue;
    }
    rows_.push_back(RowData{
        .rawKey = key,
        .displayField = QString::fromStdString(key),
        .valueText = QString::fromStdString(it->second),
        .valueType = ColumnValueType::Text,
        .source = RowSource::RemainingField,
    });
  }
}

void SongPropertiesDialog::populateTable() {
  auto *model = new QStandardItemModel(this);
  model->setColumnCount(3);
  model->setHorizontalHeaderLabels({QStringLiteral("Field"),
                                    QStringLiteral("Value"),
                                    QStringLiteral("Type")});
  model->setRowCount(static_cast<int>(rows_.size()));

  for (int rowIndex = 0; rowIndex < static_cast<int>(rows_.size());
       ++rowIndex) {
    const RowData &row = rows_[rowIndex];
    model->setItem(rowIndex, 0, new QStandardItem(row.displayField));
    model->setItem(rowIndex, 1, new QStandardItem(row.valueText));
    model->setItem(rowIndex, 2,
                   new QStandardItem(valueTypeToDisplayText(row.valueType)));
  }

  ui->properties_table->setModel(model);
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
}
