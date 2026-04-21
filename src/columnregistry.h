#ifndef COLUMNREGISTRY_H
#define COLUMNREGISTRY_H

#include "columndefinition.h"
#include <QHash>
#include <QList>
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
  bool hasColumn(const QString &id) const;
  bool isBuiltInSongAttributeKey(const QString &id) const;
  QList<ColumnDefinition> definitions() const;
  QList<ColumnDefinition> customTagDefinitions() const;
  // Contract: for non-dynamic SongAttribute columns, id must match DB table
  // `songs` column name exactly.
  QList<ColumnDefinition> songAttributeDefinitions() const;
  QList<QString> songAttributeColumnIds() const;
  QList<QString> defaultOrderedIds() const;
  bool loadDynamicColumns(QSqlDatabase &db);
  bool upsertCustomTagDefinition(QSqlDatabase &db,
                                 const ColumnDefinition &definition) const;
  bool removeCustomTagDefinition(QSqlDatabase &db,
                                 const QString &columnId) const;
  void addOrUpdateDynamicColumn(const ColumnDefinition &definition);

private:
  void add(const ColumnDefinition &definition);
  void resetDynamicColumns();

  QList<ColumnDefinition> definitions_;
  QHash<QString, int> idToIndex_;
};

#endif // COLUMNREGISTRY_H
