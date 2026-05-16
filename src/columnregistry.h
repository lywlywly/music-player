#ifndef COLUMNREGISTRY_H
#define COLUMNREGISTRY_H

#include "columndefinition.h"
#include "exprsymbolinfo.h"
#include <QHash>
#include <QList>
#include <QStringView>
#include <vector>
class QSqlDatabase;

// Source of truth for column metadata. It defines built-in columns, loads
// dynamic columns from DB table `attribute_definitions`, and provides column
// lookup.
// Layout state (order/visibility/width) is handled elsewhere.
class ColumnRegistry {
public:
  // Initializes the registry with default built-in fields.
  ColumnRegistry();

  const ColumnDefinition *findColumn(const QString &id) const;
  const FieldDefinition *findField(const QString &id) const;
  bool hasColumn(const QString &id) const;
  bool hasField(const QString &id) const;
  static QString builtinFieldId(QStringView id);
  static QString computedFieldId(QStringView idOrKey);
  static QString computedKeyFromFieldId(QStringView fieldId);
  bool isBuiltInSongAttributeKey(const QString &id) const;
  bool isReservedComputedFieldKey(const QString &key) const;
  QList<ColumnDefinition> definitions() const;
  QList<FieldDefinition> fieldDefinitions() const;
  std::vector<ExprSymbolInfo> expressionSymbols() const;
  QList<ColumnDefinition> customTagDefinitions() const;
  QList<ColumnDefinition> computedDefinitions() const;
  QList<FieldDefinition> computedFieldDefinitions() const;
  // Contract: for non-dynamic SongAttribute columns, id must match DB table
  // `songs` column name exactly.
  QList<FieldDefinition> songAttributeDefinitions() const;
  QList<QString> songAttributeColumnIds() const;
  QList<QString> defaultOrderedIds() const;
  bool loadDynamicColumns(QSqlDatabase &db);
  bool upsertCustomTagDefinition(QSqlDatabase &db,
                                 const ColumnDefinition &column,
                                 const FieldDefinition &field) const;
  bool removeCustomTagDefinition(QSqlDatabase &db,
                                 const QString &columnId) const;
  bool upsertComputedDefinition(QSqlDatabase &db,
                                const ColumnDefinition &column,
                                const FieldDefinition &field) const;
  bool removeComputedDefinition(QSqlDatabase &db,
                                const QString &columnId) const;
  void addOrUpdateDynamicColumn(const ColumnDefinition &column,
                                const FieldDefinition &field);

private:
  void add(const ColumnDefinition &column, const FieldDefinition &field);
  void addOrUpdateField(const FieldDefinition &definition);
  void resetDynamicColumns();

  QList<ColumnDefinition> definitions_;
  QHash<QString, int> idToIndex_;
  QList<FieldDefinition> fieldDefinitions_;
  QHash<QString, int> fieldIdToIndex_;
};

#endif // COLUMNREGISTRY_H
