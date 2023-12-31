#ifndef MYPROXYMODEL_H
#define MYPROXYMODEL_H

#include "playlist.h"
#include <QSortFilterProxyModel>

class MyProxyModel : public QSortFilterProxyModel, public PlayList {
  Q_OBJECT
 public:
  explicit MyProxyModel(QObject *parent = nullptr);
  void toogleSortOrder(int column);
  QList<int> getSourceIndices() override;
  QUrl getUrl(int) override;
 signals:
  void playlistChanged();

 private:
  QList<int> order;
  QList<int> indices;
};

#endif  // MYPROXYMODEL_H
