#ifndef MYPROXYMODEL_H
#define MYPROXYMODEL_H

#include <QSortFilterProxyModel>

class MyProxyModel : public QSortFilterProxyModel {
  Q_OBJECT
 public:
  explicit MyProxyModel(QObject *parent = nullptr);
  void toogleSortOrder(int column);
  QList<int> getSourceIndices();
 signals:
  void playlistChanged();

 private:
  QList<int> order;
  QList<int> indices;
};

#endif  // MYPROXYMODEL_H
