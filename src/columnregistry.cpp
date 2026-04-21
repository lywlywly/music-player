#include "columnregistry.h"
#include <QSqlError>
#include <QSqlQuery>

namespace {
bool isBuiltInSongAttribute(const ColumnDefinition &definition) {
  return definition.source == ColumnSource::SongAttribute &&
         !definition.id.startsWith("attr:");
}
} // namespace

ColumnRegistry::ColumnRegistry() {
  add({"status", "Status", ColumnSource::Computed, ColumnValueType::Text, false,
       true, 48});
  add({"artist", "Artist", ColumnSource::SongAttribute, ColumnValueType::Text,
       true, true, 180});
  add({"album", "Album", ColumnSource::SongAttribute, ColumnValueType::Text,
       true, true, 220});
  add({"discnumber", "Disc no", ColumnSource::SongAttribute,
       ColumnValueType::Number, true, true, 90});
  add({"tracknumber", "Track no", ColumnSource::SongAttribute,
       ColumnValueType::Number, true, true, 90});
  add({"title", "Title", ColumnSource::SongAttribute, ColumnValueType::Text,
       true, true, 220});
  add({"date", "Date", ColumnSource::SongAttribute, ColumnValueType::DateTime,
       true, false, 140});
  add({"genre", "Genre", ColumnSource::SongAttribute, ColumnValueType::Text,
       true, false, 140});
  add({"filepath", "File path", ColumnSource::SongAttribute,
       ColumnValueType::Text, true, false, 360});
}

const ColumnDefinition *ColumnRegistry::findColumn(const QString &id) const {
  auto it = idToIndex_.find(id);
  if (it == idToIndex_.end()) {
    return nullptr;
  }
  return &definitions_[it.value()];
}

bool ColumnRegistry::hasColumn(const QString &id) const {
  return idToIndex_.contains(id);
}

bool ColumnRegistry::isBuiltInSongAttributeKey(const QString &id) const {
  const ColumnDefinition *definition = findColumn(id);
  return definition && isBuiltInSongAttribute(*definition);
}

QList<ColumnDefinition> ColumnRegistry::definitions() const {
  return definitions_;
}

QList<ColumnDefinition> ColumnRegistry::songAttributeDefinitions() const {
  QList<ColumnDefinition> result;
  result.reserve(definitions_.size());
  for (const ColumnDefinition &definition : definitions_) {
    if (isBuiltInSongAttribute(definition)) {
      result.push_back(definition);
    }
  }
  return result;
}

QList<QString> ColumnRegistry::songAttributeColumnIds() const {
  QList<QString> ids;
  for (const ColumnDefinition &definition : songAttributeDefinitions()) {
    ids.push_back(definition.id);
  }
  return ids;
}

QList<QString> ColumnRegistry::defaultOrderedIds() const {
  QList<QString> ids;
  ids.reserve(definitions_.size());
  for (const ColumnDefinition &definition : definitions_) {
    ids.push_back(definition.id);
  }
  return ids;
}

bool ColumnRegistry::loadDynamicColumns(QSqlDatabase &db) {
  QSqlQuery query(db);
  if (!query.exec(R"(
        SELECT key, display_name, value_type, sortable, visible_default, width_default
        FROM attribute_definitions
    )")) {
    qWarning() << "ColumnRegistry loadDynamicColumns error:"
               << query.lastError().text();
    return false;
  }

  while (query.next()) {
    ColumnDefinition definition;
    definition.id = QStringLiteral("attr:") + query.value(0).toString();
    definition.title = query.value(1).toString();
    definition.source = ColumnSource::SongAttribute;
    definition.valueType =
        columnValueTypeFromStorageString(query.value(2).toString());
    definition.sortable = query.value(3).toInt() != 0;
    definition.visibleByDefault = query.value(4).toInt() != 0;
    definition.defaultWidth = query.value(5).toInt();
    if (definition.defaultWidth <= 0) {
      definition.defaultWidth = 140;
    }
    addOrUpdateDynamicColumn(definition);
  }

  return true;
}

void ColumnRegistry::add(const ColumnDefinition &definition) {
  idToIndex_.insert(definition.id, definitions_.size());
  definitions_.push_back(definition);
}

void ColumnRegistry::addOrUpdateDynamicColumn(
    const ColumnDefinition &definition) {
  auto it = idToIndex_.find(definition.id);
  if (it == idToIndex_.end()) {
    add(definition);
    return;
  }

  definitions_[it.value()] = definition;
}
