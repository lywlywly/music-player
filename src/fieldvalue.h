#ifndef FIELDVALUE_H
#define FIELDVALUE_H

#include "columndefinition.h"
#include <QString>
#include <cstdint>
#include <string>

struct FieldValue {
  // Canonical string representation loaded from DB/tag/query input.
  // Presence checks should use this field: an empty text means "no value".
  std::string text;
  // Field identity used to resolve schema/display behavior.
  std::string fieldId;
  // Parsed typed value for non-text fields.
  // If parsing fails, assign() keeps a type-appropriate default:
  // Number -> 0.0, DateTime -> 0, Boolean -> false.
  union {
    double numberDouble;
    int64_t numberInt;
    bool boolean;
  } typed{.numberInt = 0};

  FieldValue() = delete;
  FieldValue(const std::string &textValue, std::string fieldIdValue);

  void assign(const std::string &textValue, std::string fieldIdValue);
  ValueType valueType() const;
  static bool canConvert(const std::string &textValue, ValueType valueType);
  static bool parseNumber(const std::string &textValue, double &out);
  static bool parseDateTimeEpochMs(const std::string &textValue, int64_t &out);
  static bool parseBoolean(const std::string &textValue, bool &out);
  QString display() const;
  bool operator==(const FieldValue &other) const;
  operator const std::string &() const { return text; }
};

#endif // FIELDVALUE_H
