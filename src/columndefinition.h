#ifndef COLUMNDEFINITION_H
#define COLUMNDEFINITION_H

#include <QString>

enum class ColumnSource { SongAttribute, Computed };

enum class ColumnValueType { Text, Number, DateTime, Boolean };

inline QString columnValueTypeToStorageString(ColumnValueType valueType) {
  switch (valueType) {
  case ColumnValueType::Number:
    return QStringLiteral("number");
  case ColumnValueType::DateTime:
    return QStringLiteral("date");
  case ColumnValueType::Boolean:
    return QStringLiteral("boolean");
  case ColumnValueType::Text:
  default:
    return QStringLiteral("text");
  }
}

inline ColumnValueType
columnValueTypeFromStorageString(const QString &storageValue) {
  if (storageValue == QStringLiteral("number")) {
    return ColumnValueType::Number;
  }
  if (storageValue == QStringLiteral("date")) {
    return ColumnValueType::DateTime;
  }
  if (storageValue == QStringLiteral("boolean")) {
    return ColumnValueType::Boolean;
  }
  return ColumnValueType::Text;
}

struct ColumnDefinition {
  QString id;
  QString title;
  ColumnSource source;
  ColumnValueType valueType;
  QString expression;
  bool sortable = true;
  bool visibleByDefault = true;
  int defaultWidth = 140;
};

#endif // COLUMNDEFINITION_H
