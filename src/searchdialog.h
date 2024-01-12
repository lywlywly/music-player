#ifndef SEARCHDIALOG_H
#define SEARCHDIALOG_H

#include <QDialog>

#include "myproxymodel.h"
#include "mytableheader.h"
#include "songloader.h"
#include "songtablemodel.h"

namespace Ui {
class SearchDialog;
}

class SearchDialog : public QDialog {
  Q_OBJECT

 public:
  explicit SearchDialog(QWidget *parent = nullptr);
  ~SearchDialog();

 signals:
  void newSongs(const QList<Song> &);

 private:
  void setUpTable();
  void query();
  Ui::SearchDialog *ui;
  SongTableModel *model;
  MyProxyModel *proxyModel;
  MyTableHeader *tableHeader;
  SongLoader *loader;
  QList<Song> songs;
};

#endif  // SEARCHDIALOG_H
