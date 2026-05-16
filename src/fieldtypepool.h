#ifndef FIELDTYPEPOOL_H
#define FIELDTYPEPOOL_H

#include "columndefinition.h"
#include <string_view>
#include <unordered_map>
#include <vector>

struct FieldTypePoolHash {
  using is_transparent = void;

  size_t operator()(std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }

  size_t operator()(const std::string &value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }
};

struct FieldTypePoolEqual {
  using is_transparent = void;

  bool operator()(std::string_view left,
                  std::string_view right) const noexcept {
    return left == right;
  }
};

class FieldTypePool {
public:
  static FieldTypePool &instance();

  void upsert(const FieldDefinition &definition);
  void upsert(const std::vector<FieldDefinition> &definitions);
  void erase(std::string_view fieldId);

  const FieldDefinition *find(std::string_view fieldId) const;

private:
  std::unordered_map<std::string, FieldDefinition, FieldTypePoolHash,
                     FieldTypePoolEqual>
      definitionsById_;
};

#endif // FIELDTYPEPOOL_H
