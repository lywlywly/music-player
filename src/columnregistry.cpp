#include "columnregistry.h"

ColumnRegistry::ColumnRegistry() {
  add({"status", "status", ColumnSource::Computed, ColumnValueType::Text, false,
       true, 48});
  add({"artist", "artist", ColumnSource::SongAttribute, ColumnValueType::Text,
       true, true, 180});
  add({"title", "title", ColumnSource::SongAttribute, ColumnValueType::Text,
       true, true, 220});
  add({"album", "album", ColumnSource::SongAttribute, ColumnValueType::Text,
       true, true, 180});
  add({"genre", "genre", ColumnSource::SongAttribute, ColumnValueType::Text,
       true, false, 140});
  add({"comment", "comment", ColumnSource::SongAttribute, ColumnValueType::Text,
       true, false, 240});
  add({"year", "year", ColumnSource::SongAttribute, ColumnValueType::Number,
       true, false, 90});
  add({"track", "track", ColumnSource::SongAttribute, ColumnValueType::Number,
       true, false, 90});
  add({"path", "path", ColumnSource::SongAttribute, ColumnValueType::Text, true,
       false, 360});
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

QList<ColumnDefinition> ColumnRegistry::definitions() const {
  return definitions_;
}

QList<QString> ColumnRegistry::defaultOrderedIds() const {
  QList<QString> ids;
  ids.reserve(definitions_.size());
  for (const ColumnDefinition &definition : definitions_) {
    ids.push_back(definition.id);
  }
  return ids;
}

void ColumnRegistry::add(const ColumnDefinition &definition) {
  idToIndex_.insert(definition.id, definitions_.size());
  definitions_.push_back(definition);
}
