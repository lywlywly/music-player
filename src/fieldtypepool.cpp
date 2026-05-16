#include "fieldtypepool.h"

FieldTypePool &FieldTypePool::instance() {
  static FieldTypePool pool;
  return pool;
}

void FieldTypePool::upsert(const FieldDefinition &definition) {
  definitionsById_[definition.id.toStdString()] = definition;
}

void FieldTypePool::upsert(const std::vector<FieldDefinition> &definitions) {
  for (const FieldDefinition &definition : definitions) {
    upsert(definition);
  }
}

void FieldTypePool::erase(std::string_view fieldId) {
  auto it = definitionsById_.find(fieldId);
  if (it != definitionsById_.end()) {
    definitionsById_.erase(it);
  }
}

const FieldDefinition *FieldTypePool::find(std::string_view fieldId) const {
  auto it = definitionsById_.find(fieldId);
  if (it == definitionsById_.end()) {
    return nullptr;
  }
  return &it->second;
}
