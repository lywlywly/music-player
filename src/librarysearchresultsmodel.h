#ifndef LIBRARYSEARCHRESULTSMODEL_H
#define LIBRARYSEARCHRESULTSMODEL_H

#include "globalcolumnlayoutmanager.h"
#include "songlibrary.h"
#include <QAbstractTableModel>

class LibrarySearchResultsModel : public QAbstractTableModel {
  Q_OBJECT

public:
  explicit LibrarySearchResultsModel(
      SongLibrary &songLibrary, GlobalColumnLayoutManager &columnLayoutManager,
      QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;

  void setMatches(std::vector<int> songIds);
  void clear();

private:
  QList<QString> visibleSongColumnIds() const;
  QString columnIdAt(int section) const;

  SongLibrary &songLibrary_;
  GlobalColumnLayoutManager &columnLayoutManager_;
  std::vector<int> songIds_;
};

#endif // LIBRARYSEARCHRESULTSMODEL_H
