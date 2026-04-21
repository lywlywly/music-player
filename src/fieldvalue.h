#ifndef FIELDVALUE_H
#define FIELDVALUE_H

#include "columndefinition.h"
#include <cstdint>
#include <string>

struct FieldValue {
  ColumnValueType type = ColumnValueType::Text;
  std::string text;
  union {
    double number;
    int64_t epochMs;
    bool boolean;
  } typed{.epochMs = 0};

  FieldValue() = default;
  FieldValue(const std::string &textValue, ColumnValueType valueType);
  FieldValue(const char *textValue);
  FieldValue &operator=(const std::string &textValue);
  FieldValue &operator=(const char *textValue);

  void assign(const std::string &textValue, ColumnValueType valueType);
  static FieldValue fromDefinition(const ColumnDefinition *definition,
                                   const std::string &textValue);
  bool operator==(const FieldValue &other) const;
  operator const std::string &() const { return text; }
};

#endif // FIELDVALUE_H
