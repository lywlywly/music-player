#ifndef SONGTABLEMODEL_H
#define SONGTABLEMODEL_H

#include <QStandardItemModel>

#include "qurl.h"
#include "songparser.h"

extern std::map<Field, QString> fieldToStringMap;
class PlayerControlModel;
class SongTableModel : public QAbstractTableModel {
  Q_OBJECT
  // TODO: inheritance instead of composition to save extra memory and overhead
  // to maintain data structures in two classes
 public:
  SongTableModel(QObject *parent);
  // not used
  QUrl getEntryUrl(const QModelIndex &);
  // not used
  QUrl getEntryUrl(int);
  void appendSong(const QUrl songPath);
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  void save();
  void load();
  // not used
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;
 signals:
  void playlistChanged();

 private:
  QList<QString> songToItemList(const Song &song) const;
  QList<QString> fieldStringList;
  std::unique_ptr<SongParser> parser{new SongParser()};
  QVector<Song> songs;
  int columnSize;
};

#endif  // SONGTABLEMODEL_H
