#ifndef COLUMNDEFINITION_H
#define COLUMNDEFINITION_H

#include <QString>

enum class ColumnSource { SongAttribute, Computed };

enum class ColumnValueType { Text, Number, DateTime };

struct ColumnDefinition {
  QString id;
  QString title;
  ColumnSource source;
  ColumnValueType valueType;
  bool sortable = true;
  bool visibleByDefault = true;
  int defaultWidth = 140;
};

#endif // COLUMNDEFINITION_H
