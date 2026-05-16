#include "columnregistry.h"
#include "fieldtypepool.h"
#include "utils.h"
#include <QSqlError>
#include <QSqlQuery>

namespace {
constexpr QStringView kBuiltinPrefix = u"builtin:";
constexpr QStringView kComputedPrefix = u"computed:";

bool isBuiltInSongAttribute(const FieldDefinition &definition) {
  return definition.source == ColumnSource::SongAttribute &&
         !definition.id.startsWith("attr:");
}

bool isPlayStatsFieldId(const QString &id) {
  return id == QStringLiteral("play_count") ||
         id == QStringLiteral("last_played_timestamp");
}

bool isDynamicSongAttribute(const FieldDefinition &definition) {
  return definition.source == ColumnSource::SongAttribute &&
         definition.id.startsWith("attr:");
}

bool isUserComputedField(const FieldDefinition &definition) {
  return definition.source == ColumnSource::Computed &&
         !definition.expression.trimmed().isEmpty();
}
} // namespace

QString ColumnRegistry::builtinFieldId(QStringView id) {
  return QString(kBuiltinPrefix) + id;
}

QString ColumnRegistry::computedFieldId(QStringView idOrKey) {
  if (idOrKey.startsWith(kComputedPrefix)) {
    return idOrKey.toString();
  }
  return QString(kComputedPrefix) + idOrKey;
}

QString ColumnRegistry::computedKeyFromFieldId(QStringView fieldId) {
  if (fieldId.startsWith(kComputedPrefix)) {
    return fieldId.mid(9).toString();
  }
  return fieldId.toString();
}

ColumnRegistry::ColumnRegistry() {
  add({.id = "status",
       .title = "Status",
       .sortable = false,
       .visibleByDefault = true,
       .defaultWidth = 48},
      {.id = "status",
       .source = ColumnSource::Computed,
       .valueType = ValueType::Text,
       .displayKind = DisplayKind::Raw,
       .expression = "",
       .searchable = false,
       .writable = false});
  add({.id = "artist", .title = "Artist", .defaultWidth = 180},
      {.id = "artist"});
  add({.id = "album", .title = "Album", .defaultWidth = 220}, {.id = "album"});
  add({.id = "discnumber", .title = "Disc no", .defaultWidth = 90},
      {.id = "discnumber", .valueType = ValueType::Number});
  add({.id = "tracknumber", .title = "Track no", .defaultWidth = 90},
      {.id = "tracknumber", .valueType = ValueType::Number});
  add({.id = "title", .title = "Title", .defaultWidth = 220}, {.id = "title"});
  add({.id = "date", .title = "Date", .visibleByDefault = false},
      {.id = "date", .valueType = ValueType::DateTime});
  add({.id = "genre", .title = "Genre", .visibleByDefault = false},
      {.id = "genre"});
  add({.id = "codec",
       .title = "Codec",
       .visibleByDefault = false,
       .defaultWidth = 120},
      {.id = "codec"});
  add({.id = "bitrate",
       .title = "Bitrate",
       .visibleByDefault = false,
       .defaultWidth = 100},
      {.id = "bitrate", .valueType = ValueType::Number});
  add({.id = "duration",
       .title = "Duration",
       .visibleByDefault = false,
       .defaultWidth = 100},
      {.id = "duration",
       .valueType = ValueType::Number,
       .displayKind = DisplayKind::DurationSeconds});
  add({.id = "sample_rate",
       .title = "Sample rate",
       .visibleByDefault = false,
       .defaultWidth = 120},
      {.id = "sample_rate", .valueType = ValueType::Number});
  add({.id = "channels",
       .title = "Channels",
       .visibleByDefault = false,
       .defaultWidth = 80},
      {.id = "channels",
       .valueType = ValueType::Number,
       .displayKind = DisplayKind::ChannelLayout});
  add({.id = "play_count", .title = "Play Count", .defaultWidth = 120},
      {.id = "play_count", .valueType = ValueType::Number});
  add({.id = "last_played_timestamp",
       .title = "Last Played",
       .visibleByDefault = false,
       .defaultWidth = 180},
      {.id = "last_played_timestamp",
       .valueType = ValueType::DateTime,
       .displayKind = DisplayKind::EpochSecondsDateTime});
  add({.id = "filepath",
       .title = "File path",
       .visibleByDefault = false,
       .defaultWidth = 360},
      {.id = "filepath"});
}

const ColumnDefinition *ColumnRegistry::findColumn(const QString &id) const {
  auto it = idToIndex_.find(id);
  if (it == idToIndex_.end()) {
    return nullptr;
  }
  return &definitions_[it.value()];
}

const FieldDefinition *ColumnRegistry::findField(const QString &id) const {
  auto it = fieldIdToIndex_.find(id);
  if (it == fieldIdToIndex_.end()) {
    return nullptr;
  }
  return &fieldDefinitions_[it.value()];
}

bool ColumnRegistry::hasColumn(const QString &id) const {
  return idToIndex_.contains(id);
}

bool ColumnRegistry::hasField(const QString &id) const {
  return fieldIdToIndex_.contains(id);
}

bool ColumnRegistry::isBuiltInSongAttributeKey(const QString &id) const {
  const FieldDefinition *definition = findField(id);
  return definition && isBuiltInSongAttribute(*definition);
}

bool ColumnRegistry::isReservedComputedFieldKey(const QString &key) const {
  const QString normalized = util::canonicalizeTagKey(key);
  return normalized.isEmpty() || hasField(computedFieldId(normalized));
}

QList<ColumnDefinition> ColumnRegistry::definitions() const {
  return definitions_;
}

QList<FieldDefinition> ColumnRegistry::fieldDefinitions() const {
  return fieldDefinitions_;
}

std::vector<ExprSymbolInfo> ColumnRegistry::expressionSymbols() const {
  std::vector<ExprSymbolInfo> symbols;
  symbols.reserve(static_cast<size_t>(fieldDefinitions_.size() * 2));

  auto addSymbol = [&](const QString &name, const QString &resolvedId,
                       const FieldDefinition &definition) {
    symbols.push_back(ExprSymbolInfo{
        .name = util::normalizedText(name).toStdString(),
        .resolvedId = util::normalizedText(resolvedId).toStdString(),
        .valueType = definition.valueType});
  };

  for (const FieldDefinition &definition : fieldDefinitions_) {
    if (!isBuiltInSongAttribute(definition)) {
      continue;
    }
    if (!definition.searchable) {
      continue;
    }
    const QString resolvedId = builtinFieldId(definition.id);
    addSymbol(definition.id, resolvedId, definition);
    addSymbol(resolvedId, resolvedId, definition);
  }

  for (const FieldDefinition &definition : fieldDefinitions_) {
    if (!isDynamicSongAttribute(definition)) {
      continue;
    }
    if (!definition.searchable) {
      continue;
    }
    const QString key = definition.id.mid(QStringLiteral("attr:").size());
    addSymbol(key, definition.id, definition);
    addSymbol(definition.id, definition.id, definition);
  }

  for (const FieldDefinition &definition : fieldDefinitions_) {
    if (definition.source != ColumnSource::Computed) {
      continue;
    }
    if (!definition.searchable) {
      continue;
    }
    const QString resolvedId = computedFieldId(definition.id);
    const QString localName =
        resolvedId.startsWith(kComputedPrefix) ? resolvedId.mid(9) : resolvedId;
    addSymbol(localName, resolvedId, definition);
    addSymbol(resolvedId, resolvedId, definition);
  }

  return symbols;
}

QList<ColumnDefinition> ColumnRegistry::customTagDefinitions() const {
  QList<ColumnDefinition> result;
  result.reserve(definitions_.size());
  for (const ColumnDefinition &column : definitions_) {
    const FieldDefinition *field = findField(column.id);
    if (field && isDynamicSongAttribute(*field)) {
      result.push_back(column);
    }
  }
  return result;
}

QList<ColumnDefinition> ColumnRegistry::computedDefinitions() const {
  QList<ColumnDefinition> result;
  result.reserve(definitions_.size());
  for (const ColumnDefinition &column : definitions_) {
    const FieldDefinition *field = findField(column.id);
    if (field && isUserComputedField(*field)) {
      result.push_back(column);
    }
  }
  return result;
}

QList<FieldDefinition> ColumnRegistry::computedFieldDefinitions() const {
  QList<FieldDefinition> result;
  result.reserve(fieldDefinitions_.size());
  for (const FieldDefinition &definition : fieldDefinitions_) {
    if (isUserComputedField(definition)) {
      result.push_back(definition);
    }
  }
  return result;
}

QList<FieldDefinition> ColumnRegistry::songAttributeDefinitions() const {
  QList<FieldDefinition> result;
  result.reserve(fieldDefinitions_.size());
  for (const FieldDefinition &definition : fieldDefinitions_) {
    if (isBuiltInSongAttribute(definition) &&
        !isPlayStatsFieldId(definition.id)) {
      result.push_back(definition);
    }
  }
  return result;
}

QList<QString> ColumnRegistry::songAttributeColumnIds() const {
  QList<QString> ids;
  for (const FieldDefinition &definition : songAttributeDefinitions()) {
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
    const QString key = query.value(0).toString();
    const QString fieldId = QStringLiteral("attr:") + key;
    const auto valueType =
        columnValueTypeFromStorageString(query.value(2).toString());
    ColumnDefinition column{.id = fieldId,
                            .title = query.value(1).toString(),
                            .sortable = query.value(3).toInt() != 0,
                            .visibleByDefault = query.value(4).toInt() != 0,
                            .defaultWidth = query.value(5).toInt()};
    if (column.defaultWidth <= 0) {
      column.defaultWidth = 140;
    }
    FieldDefinition field{.id = fieldId,
                          .source = ColumnSource::SongAttribute,
                          .valueType = valueType,
                          .displayKind = DisplayKind::Raw,
                          .expression = "",
                          .searchable = true,
                          .writable = true};
    addOrUpdateDynamicColumn(column, field);
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
    const QString key = computedQuery.value(0).toString();
    const QString fieldId = computedFieldId(key);
    const auto valueType =
        columnValueTypeFromStorageString(computedQuery.value(2).toString());
    ColumnDefinition column{.id = fieldId,
                            .title = computedQuery.value(1).toString(),
                            .sortable = computedQuery.value(4).toInt() != 0,
                            .visibleByDefault =
                                computedQuery.value(5).toInt() != 0,
                            .defaultWidth = computedQuery.value(6).toInt()};
    if (column.defaultWidth <= 0) {
      column.defaultWidth = 140;
    }
    FieldDefinition field{.id = fieldId,
                          .source = ColumnSource::Computed,
                          .valueType = valueType,
                          .displayKind = DisplayKind::Raw,
                          .expression = computedQuery.value(3).toString(),
                          .searchable = true,
                          .writable = false};
    addOrUpdateDynamicColumn(column, field);
  }

  return true;
}

bool ColumnRegistry::upsertCustomTagDefinition(
    QSqlDatabase &db, const ColumnDefinition &column,
    const FieldDefinition &field) const {
  const QString columnId = column.id;
  if (!columnId.startsWith("attr:")) {
    qWarning() << "upsertCustomTagDefinition: invalid definition id/source"
               << columnId;
    return false;
  }

  const QString key = columnId.mid(QStringLiteral("attr:").size());
  if (key.isEmpty()) {
    qWarning() << "upsertCustomTagDefinition: empty key";
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
  query.bindValue(":display_name", column.title);
  query.bindValue(":value_type",
                  columnValueTypeToStorageString(field.valueType));
  query.bindValue(":source", "custom_tag");
  query.bindValue(":sortable", column.sortable ? 1 : 0);
  query.bindValue(":filterable", 1);
  query.bindValue(":visible_default", column.visibleByDefault ? 1 : 0);
  query.bindValue(":width_default",
                  column.defaultWidth > 0 ? column.defaultWidth : 140);
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
    QSqlDatabase &db, const ColumnDefinition &column,
    const FieldDefinition &field) const {
  const QString key =
      util::canonicalizeTagKey(computedKeyFromFieldId(column.id.trimmed()));
  if (key.isEmpty()) {
    qWarning() << "upsertComputedDefinition: empty key";
    return false;
  }
  const FieldDefinition *existing = findField(computedFieldId(key));
  if (existing && !isUserComputedField(*existing)) {
    qWarning() << "upsertComputedDefinition: key collides with reserved field"
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
  query.bindValue(":display_name", column.title);
  query.bindValue(":value_type",
                  columnValueTypeToStorageString(field.valueType));
  query.bindValue(":expression", field.expression);
  query.bindValue(":sortable", column.sortable ? 1 : 0);
  query.bindValue(":visible_default", column.visibleByDefault ? 1 : 0);
  query.bindValue(":width_default",
                  column.defaultWidth > 0 ? column.defaultWidth : 140);
  if (!query.exec()) {
    qWarning() << "upsertComputedDefinition failed:" << query.lastError();
    return false;
  }
  return true;
}

bool ColumnRegistry::removeComputedDefinition(QSqlDatabase &db,
                                              const QString &columnId) const {
  const QString key =
      util::canonicalizeTagKey(computedKeyFromFieldId(columnId.trimmed()));
  if (key.isEmpty()) {
    qWarning() << "removeComputedDefinition: empty key";
    return false;
  }

  QSqlQuery deleteDefinition(db);
  deleteDefinition.prepare(R"(
      DELETE FROM computed_attribute_definitions
      WHERE key=:key
  )");
  deleteDefinition.bindValue(":key", key);
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
  deleteValues.bindValue(":key", key);
  if (!deleteValues.exec()) {
    qWarning() << "removeComputedDefinition value delete failed:"
               << deleteValues.lastError();
    return false;
  }

  return true;
}

void ColumnRegistry::add(const ColumnDefinition &column,
                         const FieldDefinition &field) {
  addOrUpdateField(field);
  idToIndex_.insert(column.id, definitions_.size());
  definitions_.push_back(column);
}

void ColumnRegistry::addOrUpdateField(const FieldDefinition &definition) {
  FieldTypePool::instance().upsert(definition);
  if (isBuiltInSongAttribute(definition)) {
    FieldDefinition alias = definition;
    alias.id = builtinFieldId(definition.id);
    FieldTypePool::instance().upsert(alias);
  } else if (definition.source == ColumnSource::Computed) {
    const QString aliasId = computedFieldId(definition.id);
    if (aliasId != definition.id) {
      FieldDefinition alias = definition;
      alias.id = aliasId;
      FieldTypePool::instance().upsert(alias);
    }
  }

  auto it = fieldIdToIndex_.find(definition.id);
  if (it == fieldIdToIndex_.end()) {
    fieldIdToIndex_.insert(definition.id, fieldDefinitions_.size());
    fieldDefinitions_.push_back(definition);
    return;
  }
  fieldDefinitions_[it.value()] = definition;
}

void ColumnRegistry::resetDynamicColumns() {
  for (const FieldDefinition &definition : fieldDefinitions_) {
    if (isDynamicSongAttribute(definition) || isUserComputedField(definition)) {
      FieldTypePool::instance().erase(definition.id.toStdString());
      if (isUserComputedField(definition)) {
        const QString aliasId = computedFieldId(definition.id);
        if (aliasId != definition.id) {
          FieldTypePool::instance().erase(aliasId.toStdString());
        }
      }
    }
  }

  QList<ColumnDefinition> builtIns;
  builtIns.reserve(definitions_.size());
  QList<FieldDefinition> builtInFields;
  builtInFields.reserve(fieldDefinitions_.size());

  for (const FieldDefinition &definition : fieldDefinitions_) {
    if (!isDynamicSongAttribute(definition) &&
        !isUserComputedField(definition)) {
      builtInFields.push_back(definition);
    }
  }
  for (const ColumnDefinition &definition : definitions_) {
    const FieldDefinition *field = findField(definition.id);
    if (field && !isDynamicSongAttribute(*field) &&
        !isUserComputedField(*field)) {
      builtIns.push_back(definition);
    }
  }

  definitions_ = std::move(builtIns);
  idToIndex_.clear();
  for (int i = 0; i < definitions_.size(); ++i) {
    idToIndex_.insert(definitions_[i].id, i);
  }

  fieldDefinitions_ = std::move(builtInFields);
  fieldIdToIndex_.clear();
  for (int i = 0; i < fieldDefinitions_.size(); ++i) {
    fieldIdToIndex_.insert(fieldDefinitions_[i].id, i);
  }
}

void ColumnRegistry::addOrUpdateDynamicColumn(const ColumnDefinition &column,
                                              const FieldDefinition &field) {
  addOrUpdateField(field);

  auto it = idToIndex_.find(column.id);
  if (it == idToIndex_.end()) {
    idToIndex_.insert(column.id, definitions_.size());
    definitions_.push_back(column);
    return;
  }
  definitions_[it.value()] = column;
}
