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

void MyProxyModel::toogleSortOrder(int column) {
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
  // QList<int> source
  qDebug() << mapToSource(index(0, 0));
}
