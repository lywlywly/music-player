#ifndef COLUMNREGISTRY_H
#define COLUMNREGISTRY_H

#include "columndefinition.h"
#include <QHash>
#include <QList>

class ColumnRegistry {
public:
  ColumnRegistry();

  const ColumnDefinition *findColumn(const QString &id) const;
  bool hasColumn(const QString &id) const;
  QList<ColumnDefinition> definitions() const;
  QList<QString> defaultOrderedIds() const;

private:
  void add(const ColumnDefinition &definition);

  QList<ColumnDefinition> definitions_;
  QHash<QString, int> idToIndex_;
};

#endif // COLUMNREGISTRY_H
