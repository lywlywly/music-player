#ifndef GLOBALCOLUMNLAYOUTMANAGER_H
#define GLOBALCOLUMNLAYOUTMANAGER_H

#include "columnregistry.h"
#include <QHash>
#include <QObject>
#include <QSet>

// Stores global playlist-column layout in QSettings (order, visibility, width),
// normalizes it against the current registry, and emits layoutChanged().
class GlobalColumnLayoutManager : public QObject {
  Q_OBJECT

public:
  explicit GlobalColumnLayoutManager(const ColumnRegistry &registry,
                                     QObject *parent = nullptr);

  QList<QString> visibleColumnIds() const;
  QList<QString> allOrderedColumnIds() const;
  bool isColumnVisible(const QString &id) const;
  void setColumnVisible(const QString &id, bool visible);
  int columnWidth(const QString &id) const;
  void setColumnWidth(const QString &id, int width);
  void setOrder(const QList<QString> &orderedIds);
  // Used at startup after the registry is loaded from DB. It normalizes the
  // persisted layout against current columns (drop unknown ids, append new
  // ids, apply defaults for new columns, keep at least one visible), then
  // persists and emits layoutChanged() only if order/visibility changed.
  void refreshFromRegistry();
  const ColumnRegistry &registry() const;

signals:
  void layoutChanged();

private:
  void load();
  void persist() const;
  QList<QString> normalizeOrder(const QList<QString> &orderedIds) const;

  const ColumnRegistry &registry_;
  QList<QString> orderedIds_;
  QSet<QString> hiddenIds_;
  QHash<QString, int> columnWidths_;
};

#endif // GLOBALCOLUMNLAYOUTMANAGER_H
