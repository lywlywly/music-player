#ifndef MYPROXYMODEL_H
#define MYPROXYMODEL_H

#include <QSortFilterProxyModel>

#include "iplaylist.h"

class MyProxyModel : public QSortFilterProxyModel, virtual public IPlayList {
  Q_OBJECT
  Q_INTERFACES(IPlayList)
 public:
  explicit MyProxyModel(QObject *parent = nullptr);
  void toogleSortOrder(int column);
  QList<int> getSourceIndices() override;
  QUrl getUrl(int) override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
 public slots:
  void onPlayListQueueChange(QList<int>);
 signals:
  void playlistChanged() override;

 private:
  QList<int> order;
  QList<int> indices;
  QList<int> queue;
};

#endif  // MYPROXYMODEL_H
