#include "myproxymodel.h"

MyProxyModel::MyProxyModel(QObject* parent) : QSortFilterProxyModel{parent} {
  for (int i = 0; i < 8; i++) {
    order.append(0);
  }
}

QList<int> MyProxyModel::getSourceIndices() {
  indices.clear();
  for (int i = 0; i < rowCount(); i++) {
    indices.append(mapToSource(index(i, 0)).row());
  }
  return indices;
}

QVariant MyProxyModel::data(const QModelIndex& index, int role) const {
  if (role == Qt::DisplayRole) {
    // playlist queue
    if (index.column() == 0) {
      int position = queue.indexOf(index.row());
      return position > 0    ? QVariant{position}
             : position == 0 ? QVariant{"\u25B6"}
                             // BLACK RIGHT-POINTING TRIANGLE ▶ symbol
                             : QVariant{};
    }
    return QSortFilterProxyModel::data(index, role);
  }
  return QVariant{};
}

void MyProxyModel::onPlayListQueueChange(QList<int> queue) {
  this->queue = queue;
  emit dataChanged(index(0, 0), index(rowCount(), 0));
}

QUrl MyProxyModel::getUrl(int i) { return data(index(i, 8)).toUrl(); }

void MyProxyModel::toogleSortOrder(int column) {
  // qDebug() << "toogleSortOrder" << column;
  int& orderPolicy = this->order[column];
  if (orderPolicy == 0) {
    sort(column, Qt::DescendingOrder);
    orderPolicy = 1;
  } else if (orderPolicy == 1) {
    sort(column, Qt::AscendingOrder);
    orderPolicy = -1;
  } else if (orderPolicy == -1) {
    sort(column, Qt::DescendingOrder);
    orderPolicy = 1;
  }
  emit playlistChanged();
  qDebug() << mapToSource(index(0, 0));
}
