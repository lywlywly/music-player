#ifndef FIELDVALUE_H
#define FIELDVALUE_H

#include "columndefinition.h"
#include <cstdint>
#include <string>

struct FieldValue {
  // Declared field type from column definition.
  ColumnValueType type = ColumnValueType::Text;

  // Canonical string representation loaded from DB/tag/query input.
  // Presence checks should use this field: an empty text means "no value".
  std::string text;

  // Parsed typed cache for non-text fields.
  // Number   -> typed.number
  // DateTime -> typed.epochMs (Unix epoch milliseconds)
  // Boolean  -> typed.boolean
  //
  // Note: this cache alone is not a reliable presence signal because failed
  // parses keep default values. Use `text.empty()` to test value presence.
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
  static bool canConvert(const std::string &textValue,
                         ColumnValueType valueType);
  bool operator==(const FieldValue &other) const;
  operator const std::string &() const { return text; }
};

#endif // FIELDVALUE_H
