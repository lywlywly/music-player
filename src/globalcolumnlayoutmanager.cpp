#include "globalcolumnlayoutmanager.h"

#include <QSettings>

namespace {
constexpr auto kOrderKey = "playlist/columns/order";
constexpr auto kHiddenKey = "playlist/columns/hidden";
constexpr auto kWidthPrefix = "playlist/columns/width/";
} // namespace

GlobalColumnLayoutManager::GlobalColumnLayoutManager(
    const ColumnRegistry &registry, QObject *parent)
    : QObject(parent), registry_(registry) {
  load();
}

QList<QString> GlobalColumnLayoutManager::visibleColumnIds() const {
  QList<QString> ids;
  ids.reserve(orderedIds_.size());
  for (const QString &id : orderedIds_) {
    if (!hiddenIds_.contains(id)) {
      ids.push_back(id);
    }
  }
  return ids;
}

QList<QString> GlobalColumnLayoutManager::allOrderedColumnIds() const {
  return orderedIds_;
}

bool GlobalColumnLayoutManager::isColumnVisible(const QString &id) const {
  return registry_.hasColumn(id) && !hiddenIds_.contains(id);
}

void GlobalColumnLayoutManager::setColumnVisible(const QString &id,
                                                 bool visible) {
  if (!registry_.hasColumn(id)) {
    return;
  }

  const bool currentlyVisible = !hiddenIds_.contains(id);
  if (currentlyVisible == visible) {
    return;
  }

  if (visible) {
    hiddenIds_.remove(id);
  } else {
    if (visibleColumnIds().size() <= 1) {
      return;
    }
    hiddenIds_.insert(id);
  }

  persist();
  emit layoutChanged();
}

int GlobalColumnLayoutManager::columnWidth(const QString &id) const {
  if (columnWidths_.contains(id)) {
    return columnWidths_.value(id);
  }

  const ColumnDefinition *definition = registry_.findColumn(id);
  return definition ? definition->defaultWidth : 140;
}

void GlobalColumnLayoutManager::setColumnWidth(const QString &id, int width) {
  if (!registry_.hasColumn(id) || width <= 0) {
    return;
  }

  if (columnWidths_.value(id, -1) == width) {
    return;
  }

  columnWidths_.insert(id, width);
  persist();
  emit layoutChanged();
}

void GlobalColumnLayoutManager::setOrder(const QList<QString> &orderedIds) {
  QList<QString> normalized = normalizeOrder(orderedIds);
  if (normalized == orderedIds_) {
    return;
  }

  orderedIds_ = std::move(normalized);
  persist();
  emit layoutChanged();
}

const ColumnRegistry &GlobalColumnLayoutManager::registry() const {
  return registry_;
}

void GlobalColumnLayoutManager::load() {
  QSettings settings;
  QStringList orderList = settings.value(kOrderKey).toStringList();
  const bool firstRunLayout = orderList.isEmpty();

  if (firstRunLayout) {
    orderedIds_ = registry_.defaultOrderedIds();
  } else {
    QList<QString> loadedOrder;
    loadedOrder.reserve(orderList.size());
    for (const QString &id : orderList) {
      loadedOrder.push_back(id);
    }
    orderedIds_ = normalizeOrder(loadedOrder);
  }

  hiddenIds_.clear();
  const QStringList hiddenList = settings.value(kHiddenKey).toStringList();
  for (const QString &id : hiddenList) {
    if (registry_.hasColumn(id)) {
      hiddenIds_.insert(id);
    }
  }
  if (firstRunLayout) {
    for (const ColumnDefinition &definition : registry_.definitions()) {
      if (!definition.visibleByDefault) {
        hiddenIds_.insert(definition.id);
      }
    }
  }
  if (visibleColumnIds().isEmpty()) {
    const QList<QString> defaults = registry_.defaultOrderedIds();
    if (!defaults.isEmpty()) {
      hiddenIds_.remove(defaults.front());
    }
  }

  columnWidths_.clear();
  for (const QString &id : registry_.defaultOrderedIds()) {
    const QString key = QString::fromUtf8(kWidthPrefix) + id;
    if (settings.contains(key)) {
      const int width = settings.value(key).toInt();
      if (width > 0) {
        columnWidths_.insert(id, width);
      }
    }
  }

  persist();
}

void GlobalColumnLayoutManager::persist() const {
  QSettings settings;

  QStringList orderList;
  orderList.reserve(orderedIds_.size());
  for (const QString &id : orderedIds_) {
    orderList.push_back(id);
  }
  settings.setValue(kOrderKey, orderList);

  QStringList hiddenList;
  hiddenList.reserve(hiddenIds_.size());
  for (const QString &id : hiddenIds_) {
    hiddenList.push_back(id);
  }
  settings.setValue(kHiddenKey, hiddenList);

  for (const QString &id : registry_.defaultOrderedIds()) {
    const QString key = QString::fromUtf8(kWidthPrefix) + id;
    if (columnWidths_.contains(id)) {
      settings.setValue(key, columnWidths_.value(id));
    } else {
      settings.remove(key);
    }
  }
}

QList<QString> GlobalColumnLayoutManager::normalizeOrder(
    const QList<QString> &orderedIds) const {
  QSet<QString> seen;
  QList<QString> normalized;
  normalized.reserve(registry_.defaultOrderedIds().size());

  for (const QString &id : orderedIds) {
    if (!registry_.hasColumn(id) || seen.contains(id)) {
      continue;
    }
    normalized.push_back(id);
    seen.insert(id);
  }

  for (const QString &id : registry_.defaultOrderedIds()) {
    if (!seen.contains(id)) {
      normalized.push_back(id);
    }
  }

  return normalized;
}
