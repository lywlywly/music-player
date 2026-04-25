#ifndef LIBRARYSEARCHDIALOG_H
#define LIBRARYSEARCHDIALOG_H

#include "globalcolumnlayoutmanager.h"
#include "librarysearchresultsmodel.h"
#include "songlibrary.h"
#include <QDialog>

namespace Ui {
class LibrarySearchDialog;
}

class LibrarySearchDialog : public QDialog {
  Q_OBJECT

public:
  explicit LibrarySearchDialog(SongLibrary &songLibrary,
                               GlobalColumnLayoutManager &columnLayoutManager,
                               QWidget *parent = nullptr);
  ~LibrarySearchDialog();

signals:
  void createPlaylistRequested(const QList<int> &songIds);

private:
  void createPlaylistFromCurrentResults();
  void updateCreatePlaylistButtonState();
  void performSearch();
  void showMatchCount();
  void showError(const ExprParseError &error);

  Ui::LibrarySearchDialog *ui = nullptr;
  SongLibrary &songLibrary_;
  LibrarySearchResultsModel *resultsModel_ = nullptr;
  std::vector<int> currentMatches_;
};

#endif // LIBRARYSEARCHDIALOG_H
