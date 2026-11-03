#ifndef LIBRARYSEARCHDIALOG_H
#define LIBRARYSEARCHDIALOG_H

#include "globalcolumnlayoutmanager.h"
#include "librarysearchresultsmodel.h"
#include "songlibrary.h"
#include <QDialog>

class MonitoredLibrary;

namespace Ui {
class LibrarySearchDialog;
}

// Searches the monitored library with a condition (parseSongFilter). Results
// can become a manual playlist, or the expression an autoplaylist.
class LibrarySearchDialog : public QDialog {
  Q_OBJECT

public:
  LibrarySearchDialog(SongLibrary &songLibrary,
                      const MonitoredLibrary &monitoredLibrary,
                      GlobalColumnLayoutManager &columnLayoutManager,
                      QWidget *parent = nullptr);
  ~LibrarySearchDialog();

signals:
  void createPlaylistRequested(const QList<int> &songIds);
  void saveAsAutoPlaylistRequested(const QString &expression);

private:
  void createPlaylistFromCurrentResults();
  void performSearch();
  // Clears the results and disables the result actions.
  void clearResults();
  void showMessage(const QString &message, bool error);

  Ui::LibrarySearchDialog *ui = nullptr;
  SongLibrary &songLibrary_;
  const MonitoredLibrary &monitoredLibrary_;
  LibrarySearchResultsModel *resultsModel_ = nullptr;
  std::vector<int> currentMatches_;
  // The expression of the last successful search.
  QString currentExpression_;
};

#endif // LIBRARYSEARCHDIALOG_H
