#ifndef PLAYLISTSEARCHDIALOG_H
#define PLAYLISTSEARCHDIALOG_H

#include <QDialog>
#include <QPointer>
#include <vector>

class GlobalColumnLayoutManager;
class LibrarySearchResultsModel;
class Playlist;
class PlaylistCollection;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;
class QTimer;
class SongLibrary;

// Non-modal search over the playlist shown in the UI (SongMatcher). Results
// follow the query and the playlist's rows (debounced). Actions on a result
// look its row up by pk when they run and are emitted as row signals of the
// target playlist; a result no longer in the playlist does nothing.
class PlaylistSearchDialog : public QDialog {
  Q_OBJECT

public:
  PlaylistSearchDialog(SongLibrary &songLibrary,
                       const PlaylistCollection &collection,
                       GlobalColumnLayoutManager &columnLayoutManager,
                       QWidget *parent = nullptr);

  // Searches playlist (nullptr: none) from now on.
  void setPlaylist(Playlist *playlist);

signals:
  // Double-click or Enter on a result.
  void playRequested(int row);
  void showRequested(int row);
  void queueRequested(int row);
  void propertiesRequested(int row);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void scheduleSearch();
  void performSearch();
  void updateTitle();
  void updateActionsEnabled();
  // The selected result's index in resultPks_, or -1.
  int selectedResult() const;
  // Emits signal with the target playlist's row of result (an index in
  // resultPks_), if the playlist still has that song.
  void emitForResult(int result, void (PlaylistSearchDialog::*signal)(int));

  SongLibrary &songLibrary_;
  const PlaylistCollection &collection_;
  GlobalColumnLayoutManager &columnLayoutManager_;
  QPointer<Playlist> playlist_;
  QLineEdit *queryEdit_ = nullptr;
  QLabel *modeLabel_ = nullptr;
  QLabel *statusLabel_ = nullptr;
  QTableView *resultsTable_ = nullptr;
  QPushButton *showButton_ = nullptr;
  QPushButton *queueButton_ = nullptr;
  QPushButton *propertiesButton_ = nullptr;
  LibrarySearchResultsModel *resultsModel_ = nullptr;
  QTimer *searchTimer_ = nullptr;
  std::vector<int> resultPks_;
};

#endif // PLAYLISTSEARCHDIALOG_H
