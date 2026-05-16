#ifndef COLUMNDEFINITION_H
#define COLUMNDEFINITION_H

#include <QString>

enum class ColumnSource { SongAttribute, Computed };

enum class ValueType { Text, Number, DateTime, Boolean };

enum class DisplayKind {
  Raw,
  EpochSecondsDateTime,
  DurationSeconds,
  ChannelLayout
};

inline QString columnValueTypeToStorageString(ValueType valueType) {
  switch (valueType) {
  case ValueType::Number:
    return QStringLiteral("number");
  case ValueType::DateTime:
    return QStringLiteral("date");
  case ValueType::Boolean:
    return QStringLiteral("boolean");
  case ValueType::Text:
  default:
    return QStringLiteral("text");
  }
}

inline ValueType columnValueTypeFromStorageString(const QString &storageValue) {
  if (storageValue == QStringLiteral("number")) {
    return ValueType::Number;
  }
  if (storageValue == QStringLiteral("date")) {
    return ValueType::DateTime;
  }
  if (storageValue == QStringLiteral("boolean")) {
    return ValueType::Boolean;
  }
  return ValueType::Text;
}

struct ColumnDefinition {
  QString id;
  QString title;
  bool sortable = true;
  bool visibleByDefault = true;
  int defaultWidth = 140;
};

struct FieldDefinition {
  QString id;
  ColumnSource source = ColumnSource::SongAttribute;
  ValueType valueType = ValueType::Text;
  DisplayKind displayKind = DisplayKind::Raw;
  QString expression;
  bool searchable = true;
  bool writable = true;
};

#endif // COLUMNDEFINITION_H
