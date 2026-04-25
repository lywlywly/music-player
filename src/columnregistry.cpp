#include "columnregistry.h"
#include <QSqlError>
#include <QSqlQuery>

namespace {
bool isBuiltInSongAttribute(const ColumnDefinition &definition) {
  return definition.source == ColumnSource::SongAttribute &&
         !definition.id.startsWith("attr:");
}

bool isDynamicSongAttribute(const ColumnDefinition &definition) {
  return definition.source == ColumnSource::SongAttribute &&
         definition.id.startsWith("attr:");
}

bool isUserComputedField(const ColumnDefinition &definition) {
  return definition.source == ColumnSource::Computed &&
         !definition.expression.trimmed().isEmpty();
}
} // namespace

ColumnRegistry::ColumnRegistry() {
  add({"status", "Status", ColumnSource::Computed, ColumnValueType::Text, "",
       false, true, 48});
  add({"artist", "Artist", ColumnSource::SongAttribute, ColumnValueType::Text,
       "", true, true, 180});
  add({"album", "Album", ColumnSource::SongAttribute, ColumnValueType::Text, "",
       true, true, 220});
  add({"discnumber", "Disc no", ColumnSource::SongAttribute,
       ColumnValueType::Number, "", true, true, 90});
  add({"tracknumber", "Track no", ColumnSource::SongAttribute,
       ColumnValueType::Number, "", true, true, 90});
  add({"title", "Title", ColumnSource::SongAttribute, ColumnValueType::Text, "",
       true, true, 220});
  add({"date", "Date", ColumnSource::SongAttribute, ColumnValueType::DateTime,
       "", true, false, 140});
  add({"genre", "Genre", ColumnSource::SongAttribute, ColumnValueType::Text, "",
       true, false, 140});
  add({"filepath", "File path", ColumnSource::SongAttribute,
       ColumnValueType::Text, "", true, false, 360});
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

bool ColumnRegistry::isReservedComputedFieldKey(const QString &key) const {
  if (key.isEmpty()) {
    return true;
  }
  if (isBuiltInSongAttributeKey(key)) {
    return true;
  }
  if (hasColumn(QStringLiteral("attr:") + key)) {
    return true;
  }
  const ColumnDefinition *definition = findColumn(key);
  return definition != nullptr;
}

QList<ColumnDefinition> ColumnRegistry::definitions() const {
  return definitions_;
}

QList<ColumnDefinition> ColumnRegistry::customTagDefinitions() const {
  QList<ColumnDefinition> result;
  result.reserve(definitions_.size());
  for (const ColumnDefinition &definition : definitions_) {
    if (isDynamicSongAttribute(definition)) {
      result.push_back(definition);
    }
  }
  return result;
}

QList<ColumnDefinition> ColumnRegistry::computedDefinitions() const {
  QList<ColumnDefinition> result;
  result.reserve(definitions_.size());
  for (const ColumnDefinition &definition : definitions_) {
    if (isUserComputedField(definition)) {
      result.push_back(definition);
    }
  }
  return result;
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
  resetDynamicColumns();

  QSqlQuery query(db);
  if (!query.exec(R"(
        SELECT key, display_name, value_type, sortable, visible_default, width_default
        FROM attribute_definitions
        WHERE source IN ('user', 'custom_tag')
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

  QSqlQuery computedQuery(db);
  if (!computedQuery.exec(R"(
        SELECT key, display_name, value_type, expression, sortable, visible_default, width_default
        FROM computed_attribute_definitions
    )")) {
    if (computedQuery.lastError().text().contains("no such table",
                                                  Qt::CaseInsensitive)) {
      return true;
    }
    qWarning() << "ColumnRegistry loadDynamicColumns computed error:"
               << computedQuery.lastError().text();
    return false;
  }

  while (computedQuery.next()) {
    ColumnDefinition definition;
    definition.id = computedQuery.value(0).toString();
    definition.title = computedQuery.value(1).toString();
    definition.source = ColumnSource::Computed;
    definition.valueType =
        columnValueTypeFromStorageString(computedQuery.value(2).toString());
    definition.expression = computedQuery.value(3).toString();
    definition.sortable = computedQuery.value(4).toInt() != 0;
    definition.visibleByDefault = computedQuery.value(5).toInt() != 0;
    definition.defaultWidth = computedQuery.value(6).toInt();
    if (definition.defaultWidth <= 0) {
      definition.defaultWidth = 140;
    }
    addOrUpdateDynamicColumn(definition);
  }

  return true;
}

bool ColumnRegistry::upsertCustomTagDefinition(
    QSqlDatabase &db, const ColumnDefinition &definition) const {
  if (definition.source != ColumnSource::SongAttribute ||
      !definition.id.startsWith("attr:")) {
    qWarning() << "upsertCustomTagDefinition: invalid definition id/source"
               << definition.id;
    return false;
  }

  const QString key = definition.id.mid(QStringLiteral("attr:").size());
  if (key.isEmpty()) {
    qWarning() << "upsertCustomTagDefinition: empty key";
    return false;
  }
  if (isBuiltInSongAttributeKey(key)) {
    qWarning() << "upsertCustomTagDefinition: key collides with built-in field"
               << key;
    return false;
  }
  if (hasColumn(key)) {
    qWarning() << "upsertCustomTagDefinition: key collides with computed field"
               << key;
    return false;
  }

  QSqlQuery query(db);
  query.prepare(R"(
      INSERT INTO attribute_definitions(
          key, display_name, value_type, source, sortable, filterable,
          visible_default, width_default, enum_values_json
      ) VALUES(
          :key, :display_name, :value_type, :source, :sortable, :filterable,
          :visible_default, :width_default, NULL
      )
      ON CONFLICT(key) DO UPDATE SET
          display_name=excluded.display_name,
          value_type=excluded.value_type,
          source=excluded.source,
          sortable=excluded.sortable,
          filterable=excluded.filterable,
          visible_default=excluded.visible_default,
          width_default=excluded.width_default
  )");
  query.bindValue(":key", key);
  query.bindValue(":display_name", definition.title);
  query.bindValue(":value_type",
                  columnValueTypeToStorageString(definition.valueType));
  query.bindValue(":source", "custom_tag");
  query.bindValue(":sortable", definition.sortable ? 1 : 0);
  query.bindValue(":filterable", 1);
  query.bindValue(":visible_default", definition.visibleByDefault ? 1 : 0);
  query.bindValue(":width_default",
                  definition.defaultWidth > 0 ? definition.defaultWidth : 140);
  if (!query.exec()) {
    qWarning() << "upsertCustomTagDefinition failed:" << query.lastError();
    return false;
  }

  return true;
}

bool ColumnRegistry::removeCustomTagDefinition(QSqlDatabase &db,
                                               const QString &columnId) const {
  if (!columnId.startsWith("attr:")) {
    qWarning() << "removeCustomTagDefinition: invalid column id" << columnId;
    return false;
  }

  const QString key = columnId.mid(QStringLiteral("attr:").size());
  if (key.isEmpty()) {
    qWarning() << "removeCustomTagDefinition: empty key";
    return false;
  }

  QSqlQuery deleteDefinition(db);
  deleteDefinition.prepare(R"(
      DELETE FROM attribute_definitions
      WHERE key=:key AND source IN ('user', 'custom_tag')
  )");
  deleteDefinition.bindValue(":key", key);
  if (!deleteDefinition.exec()) {
    qWarning() << "removeCustomTagDefinition definition delete failed:"
               << deleteDefinition.lastError();
    return false;
  }

  QSqlQuery deleteValues(db);
  deleteValues.prepare(R"(
      DELETE FROM song_attributes
      WHERE key=:key
  )");
  deleteValues.bindValue(":key", key);
  if (!deleteValues.exec()) {
    qWarning() << "removeCustomTagDefinition value delete failed:"
               << deleteValues.lastError();
    return false;
  }

  return true;
}

bool ColumnRegistry::upsertComputedDefinition(
    QSqlDatabase &db, const ColumnDefinition &definition) const {
  if (definition.source != ColumnSource::Computed) {
    qWarning() << "upsertComputedDefinition: invalid source";
    return false;
  }
  const QString key = definition.id.trimmed();
  if (key.isEmpty()) {
    qWarning() << "upsertComputedDefinition: empty key";
    return false;
  }
  if (isBuiltInSongAttributeKey(key)) {
    qWarning() << "upsertComputedDefinition: key collides with built-in field"
               << key;
    return false;
  }
  if (hasColumn(QStringLiteral("attr:") + key)) {
    qWarning() << "upsertComputedDefinition: key collides with attr field"
               << key;
    return false;
  }
  const ColumnDefinition *existing = findColumn(key);
  if (existing && !isUserComputedField(*existing)) {
    qWarning() << "upsertComputedDefinition: key collides with built-in field"
               << key;
    return false;
  }

  QSqlQuery query(db);
  query.prepare(R"(
      INSERT INTO computed_attribute_definitions(
          key, display_name, value_type, expression, sortable, visible_default, width_default, updated_at
      ) VALUES(
          :key, :display_name, :value_type, :expression, :sortable, :visible_default, :width_default, strftime('%s','now')
      )
      ON CONFLICT(key) DO UPDATE SET
          display_name=excluded.display_name,
          value_type=excluded.value_type,
          expression=excluded.expression,
          sortable=excluded.sortable,
          visible_default=excluded.visible_default,
          width_default=excluded.width_default,
          updated_at=excluded.updated_at
  )");
  query.bindValue(":key", key);
  query.bindValue(":display_name", definition.title);
  query.bindValue(":value_type",
                  columnValueTypeToStorageString(definition.valueType));
  query.bindValue(":expression", definition.expression);
  query.bindValue(":sortable", definition.sortable ? 1 : 0);
  query.bindValue(":visible_default", definition.visibleByDefault ? 1 : 0);
  query.bindValue(":width_default",
                  definition.defaultWidth > 0 ? definition.defaultWidth : 140);
  if (!query.exec()) {
    qWarning() << "upsertComputedDefinition failed:" << query.lastError();
    return false;
  }
  return true;
}

bool ColumnRegistry::removeComputedDefinition(QSqlDatabase &db,
                                              const QString &columnId) const {
  if (columnId.trimmed().isEmpty()) {
    qWarning() << "removeComputedDefinition: empty key";
    return false;
  }

  QSqlQuery deleteDefinition(db);
  deleteDefinition.prepare(R"(
      DELETE FROM computed_attribute_definitions
      WHERE key=:key
  )");
  deleteDefinition.bindValue(":key", columnId);
  if (!deleteDefinition.exec()) {
    qWarning() << "removeComputedDefinition definition delete failed:"
               << deleteDefinition.lastError();
    return false;
  }

  QSqlQuery deleteValues(db);
  deleteValues.prepare(R"(
      DELETE FROM song_computed_attributes
      WHERE key=:key
  )");
  deleteValues.bindValue(":key", columnId);
  if (!deleteValues.exec()) {
    qWarning() << "removeComputedDefinition value delete failed:"
               << deleteValues.lastError();
    return false;
  }

  return true;
}

void ColumnRegistry::add(const ColumnDefinition &definition) {
  idToIndex_.insert(definition.id, definitions_.size());
  definitions_.push_back(definition);
}

void ColumnRegistry::resetDynamicColumns() {
  QList<ColumnDefinition> builtIns;
  builtIns.reserve(definitions_.size());
  for (const ColumnDefinition &definition : definitions_) {
    if (!isDynamicSongAttribute(definition) &&
        !isUserComputedField(definition)) {
      builtIns.push_back(definition);
    }
  }

  definitions_ = std::move(builtIns);
  idToIndex_.clear();
  for (int i = 0; i < definitions_.size(); ++i) {
    idToIndex_.insert(definitions_[i].id, i);
  }
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
