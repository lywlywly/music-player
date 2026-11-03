#include "playlisttableview.h"

#include "columnregistry.h"
#include "globalcolumnlayoutmanager.h"
#include "playlisttablemodel.h"
#include <QDropEvent>
#include <QHeaderView>
#include <QMenu>
#include <QShortcut>
#include <QSignalBlocker>
#include <algorithm>

PlaylistTableView::PlaylistTableView(Playlist &playlist, PlaybackQueue &queue,
                                     const ColumnRegistry &registry,
                                     GlobalColumnLayoutManager &layout,
                                     QWidget *parent)
    : QTableView(parent), registry_(registry), layout_(layout),
      model_(new PlaylistTableModel(playlist, queue, layout, this)) {
  setModel(model_);
  setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  setSortingEnabled(true);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setSelectionBehavior(QAbstractItemView::SelectRows);
  // Drag rows to reorder the playlist (PlaylistTableModel::dropMimeData).
  setDragDropMode(QAbstractItemView::InternalMove);
  // QTableView drops onto items by default; drop between rows instead.
  setDragDropOverwriteMode(false);
  setContextMenuPolicy(Qt::CustomContextMenu);
  // The Properties menu action only displays these keys: Qt does not dispatch
  // shortcuts reliably from a transient QMenu, so the table handles them.
  for (const QKeySequence &sequence : propertiesShortcuts()) {
    auto *shortcut = new QShortcut(sequence, this);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut, &QShortcut::activated, this, [this]() {
      const int row = firstSelectedRow();
      emit propertiesRequested(row >= 0 ? row : currentIndex().row());
    });
  }

  QHeaderView *header = horizontalHeader();
  header->setSortIndicatorShown(false);
  header->setSectionsMovable(true);
  header->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(header, &QHeaderView::sectionClicked, this,
          &PlaylistTableView::sortByHeaderSection);
  connect(header, &QHeaderView::sectionMoved, this,
          &PlaylistTableView::saveVisualOrder);
  connect(header, &QHeaderView::sectionResized, this,
          [this](int logicalIndex, int, int newSize) {
            const QList<QString> visibleIds = layout_.visibleColumnIds();
            if (logicalIndex >= 0 && logicalIndex < visibleIds.size()) {
              layout_.setColumnWidth(visibleIds[logicalIndex], newSize);
            }
          });
  connect(header, &QHeaderView::customContextMenuRequested, this,
          &PlaylistTableView::showColumnsMenu);
  connect(&layout_, &GlobalColumnLayoutManager::layoutChanged, this,
          &PlaylistTableView::applyLayout);

  applyLayout();
}

QList<QKeySequence> PlaylistTableView::propertiesShortcuts() {
#ifdef Q_OS_MACOS
  return {QKeySequence(Qt::CTRL | Qt::Key_I)};
#else
  return {QKeySequence(Qt::ALT | Qt::Key_Return),
          QKeySequence(Qt::ALT | Qt::Key_Enter)};
#endif
}

int PlaylistTableView::firstSelectedRow() const {
  const QModelIndexList selectedRows = selectionModel()->selectedRows();
  if (selectedRows.isEmpty()) {
    return -1;
  }
  int firstRow = selectedRows.front().row();
  for (const QModelIndex &index : selectedRows) {
    firstRow = std::min(firstRow, index.row());
  }
  return firstRow;
}

void PlaylistTableView::sortByHeaderSection(int section) {
  QHeaderView *header = horizontalHeader();
  const QList<QString> visibleIds = layout_.visibleColumnIds();
  if (section < 0 || section >= visibleIds.size()) {
    header->setSortIndicatorShown(false);
    return;
  }

  const QString &columnId = visibleIds[section];
  const ColumnDefinition *definition = registry_.findColumn(columnId);
  if (!definition || !definition->sortable) {
    header->setSortIndicatorShown(false);
    return;
  }

  header->setSortIndicatorShown(true);
  model_->sortByColumnId(columnId, header->sortIndicatorOrder());
}

void PlaylistTableView::dropEvent(QDropEvent *event) {
  QTableView::dropEvent(event);
  // Accepted only when the rows moved (PlaylistTableModel::dropMimeData): a
  // hand-made order is no longer sorted by the indicated column.
  if (event->isAccepted()) {
    horizontalHeader()->setSortIndicatorShown(false);
  }
}

void PlaylistTableView::applyLayout() {
  QHeaderView *header = horizontalHeader();
  // The header's resize signals would write the widths back to layout_.
  const QSignalBlocker blocker(header);
  const QList<QString> visibleIds = layout_.visibleColumnIds();
  for (int col = 0; col < visibleIds.size(); ++col) {
    setColumnWidth(col, layout_.columnWidth(visibleIds[col]));
  }
  header->setSortIndicatorShown(false);
}

void PlaylistTableView::saveVisualOrder() {
  const QList<QString> currentVisibleIds = layout_.visibleColumnIds();
  const int count = currentVisibleIds.size();
  if (count <= 0) {
    return;
  }

  QList<QString> visibleByVisualIndex;
  visibleByVisualIndex.resize(count);

  QHeaderView *header = horizontalHeader();
  for (int logical = 0; logical < count; ++logical) {
    const int visual = header->visualIndex(logical);
    if (visual < 0 || visual >= count) {
      continue;
    }
    visibleByVisualIndex[visual] = currentVisibleIds[logical];
  }

  QList<QString> newOrder;
  for (const QString &id : visibleByVisualIndex) {
    if (!id.isEmpty()) {
      newOrder.push_back(id);
    }
  }

  for (const QString &id : layout_.allOrderedColumnIds()) {
    if (!layout_.isColumnVisible(id)) {
      newOrder.push_back(id);
    }
  }

  layout_.setOrder(newOrder);
}

void PlaylistTableView::showColumnsMenu(const QPoint &pos) {
  QMenu menu(this);
  for (const QString &id : layout_.allOrderedColumnIds()) {
    const ColumnDefinition *definition = registry_.findColumn(id);
    const QString title = definition ? definition->title : id;

    QAction *action = menu.addAction(title);
    action->setCheckable(true);
    action->setChecked(layout_.isColumnVisible(id));
    action->setData(id);
  }

  QAction *selected = menu.exec(horizontalHeader()->mapToGlobal(pos));
  if (!selected) {
    return;
  }

  const QString selectedId = selected->data().toString();
  const bool shouldBeVisible = selected->isChecked();

  if (!shouldBeVisible && layout_.visibleColumnIds().size() <= 1) {
    return;
  }

  layout_.setColumnVisible(selectedId, shouldBeVisible);
}
