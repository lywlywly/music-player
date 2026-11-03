#include "playlistsearchdialog.h"

#include "librarysearchresultsmodel.h"
#include "playlist.h"
#include "playlistcollection.h"
#include "playlistsearch.h"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr int kSearchDelayMs = 150;
} // namespace

PlaylistSearchDialog::PlaylistSearchDialog(
    SongLibrary &songLibrary, const PlaylistCollection &collection,
    GlobalColumnLayoutManager &columnLayoutManager, QWidget *parent)
    : QDialog(parent), songLibrary_(songLibrary), collection_(collection),
      columnLayoutManager_(columnLayoutManager) {
  resize(820, 460);

  queryEdit_ = new QLineEdit(this);
  queryEdit_->setObjectName("playlist_search_query_edit");
  queryEdit_->setPlaceholderText(
      QStringLiteral("Words, or a condition like genre HAS jazz"));
  queryEdit_->setClearButtonEnabled(true);
  modeLabel_ = new QLabel(this);
  modeLabel_->setObjectName("playlist_search_mode_label");
  auto *queryLayout = new QHBoxLayout;
  queryLayout->addWidget(queryEdit_);
  queryLayout->addWidget(modeLabel_);

  statusLabel_ = new QLabel(this);
  statusLabel_->setObjectName("playlist_search_status_label");
  statusLabel_->setWordWrap(true);

  resultsModel_ =
      new LibrarySearchResultsModel(songLibrary_, columnLayoutManager_, this);
  resultsTable_ = new QTableView(this);
  resultsTable_->setObjectName("playlist_search_results_table");
  resultsTable_->setModel(resultsModel_);
  resultsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  resultsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  resultsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  resultsTable_->horizontalHeader()->setStretchLastSection(true);
  resultsTable_->verticalHeader()->setVisible(false);

  showButton_ = new QPushButton(QStringLiteral("Show in Playlist"), this);
  showButton_->setObjectName("playlist_search_show_button");
  queueButton_ = new QPushButton(QStringLiteral("Add to Playback Queue"), this);
  queueButton_->setObjectName("playlist_search_queue_button");
  propertiesButton_ = new QPushButton(QStringLiteral("Properties"), this);
  propertiesButton_->setObjectName("playlist_search_properties_button");
  auto *buttonLayout = new QHBoxLayout;
  buttonLayout->addStretch();
  for (QPushButton *button : {showButton_, queueButton_, propertiesButton_}) {
    // Enter in the query box must not press a button.
    button->setAutoDefault(false);
    buttonLayout->addWidget(button);
  }

  auto *layout = new QVBoxLayout(this);
  layout->addLayout(queryLayout);
  layout->addWidget(statusLabel_);
  layout->addWidget(resultsTable_);
  layout->addLayout(buttonLayout);

  searchTimer_ = new QTimer(this);
  searchTimer_->setSingleShot(true);
  searchTimer_->setInterval(kSearchDelayMs);
  connect(searchTimer_, &QTimer::timeout, this,
          &PlaylistSearchDialog::performSearch);
  connect(queryEdit_, &QLineEdit::textChanged, this,
          &PlaylistSearchDialog::scheduleSearch);
  connect(&columnLayoutManager_, &GlobalColumnLayoutManager::layoutChanged,
          this, &PlaylistSearchDialog::scheduleSearch);
  connect(&collection_, &PlaylistCollection::playlistRenamed, this,
          &PlaylistSearchDialog::updateTitle);

  // Enter is handled in eventFilter: QAbstractItemView's activated is not
  // emitted for Enter on macOS.
  connect(resultsTable_, &QTableView::doubleClicked, this,
          [this](const QModelIndex &index) {
            emitForResult(index.row(), &PlaylistSearchDialog::playRequested);
          });
  resultsTable_->installEventFilter(this);
  connect(resultsTable_->selectionModel(),
          &QItemSelectionModel::selectionChanged, this,
          &PlaylistSearchDialog::updateActionsEnabled);
  connect(showButton_, &QPushButton::clicked, this, [this]() {
    emitForResult(selectedResult(), &PlaylistSearchDialog::showRequested);
  });
  connect(queueButton_, &QPushButton::clicked, this, [this]() {
    emitForResult(selectedResult(), &PlaylistSearchDialog::queueRequested);
  });
  connect(propertiesButton_, &QPushButton::clicked, this, [this]() {
    emitForResult(selectedResult(), &PlaylistSearchDialog::propertiesRequested);
  });

  updateTitle();
  updateActionsEnabled();
}

void PlaylistSearchDialog::setPlaylist(Playlist *playlist) {
  if (playlist_) {
    disconnect(playlist_, nullptr, this, nullptr);
  }
  playlist_ = playlist;
  if (playlist_) {
    connect(playlist_, &Playlist::songsInserted, this,
            &PlaylistSearchDialog::scheduleSearch);
    connect(playlist_, &Playlist::songsRemoved, this,
            &PlaylistSearchDialog::scheduleSearch);
    connect(playlist_, &Playlist::songsReset, this,
            &PlaylistSearchDialog::scheduleSearch);
    connect(playlist_, &Playlist::songsReordered, this,
            &PlaylistSearchDialog::scheduleSearch);
    connect(playlist_, &Playlist::songDataChanged, this,
            &PlaylistSearchDialog::scheduleSearch);
    connect(playlist_, &Playlist::songsRefreshed, this,
            &PlaylistSearchDialog::scheduleSearch);
  }
  updateTitle();
  performSearch();
}

bool PlaylistSearchDialog::eventFilter(QObject *watched, QEvent *event) {
  if (watched == resultsTable_ && event->type() == QEvent::KeyPress) {
    const int key = static_cast<QKeyEvent *>(event)->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      emitForResult(resultsTable_->currentIndex().row(),
                    &PlaylistSearchDialog::playRequested);
      return true;
    }
  }
  return QDialog::eventFilter(watched, event);
}

void PlaylistSearchDialog::scheduleSearch() { searchTimer_->start(); }

void PlaylistSearchDialog::performSearch() {
  searchTimer_->stop();
  // Results are replaced, so the selection is restored by pk.
  const int selected = selectedResult();
  const int selectedPk = selected >= 0 ? resultPks_[selected] : -1;
  resultPks_.clear();
  const QString query = queryEdit_->text();
  if (!playlist_ || query.trimmed().isEmpty()) {
    modeLabel_->clear();
    statusLabel_->clear();
    resultsModel_->clear();
    updateActionsEnabled();
    return;
  }

  const SongMatcher matcher(songLibrary_, query,
                            columnLayoutManager_.visibleColumnIds());
  statusLabel_->setStyleSheet(QString());
  switch (matcher.mode()) {
  case SongMatcher::Mode::Text:
    modeLabel_->setText(QStringLiteral("Text"));
    break;
  case SongMatcher::Mode::Expression:
    modeLabel_->setText(QStringLiteral("Expression"));
    break;
  case SongMatcher::Mode::Error:
    modeLabel_->setText(QStringLiteral("Expression"));
    statusLabel_->setStyleSheet(QStringLiteral("color: #b42318;"));
    statusLabel_->setText(matcher.error());
    resultsModel_->clear();
    updateActionsEnabled();
    return;
  }

  resultPks_ = matchPlaylistRows(*playlist_, matcher);
  resultsModel_->setMatches(resultPks_);
  const auto it = std::find(resultPks_.begin(), resultPks_.end(), selectedPk);
  if (it != resultPks_.end()) {
    resultsTable_->selectRow(static_cast<int>(it - resultPks_.begin()));
  }
  statusLabel_->setText(
      resultPks_.size() == 1
          ? QStringLiteral("1 match")
          : QStringLiteral("%1 matches").arg(resultPks_.size()));
  updateActionsEnabled();
}

void PlaylistSearchDialog::updateTitle() {
  setWindowTitle(playlist_ ? QStringLiteral("Search Playlist: %1")
                                 .arg(collection_.name(playlist_->id()))
                           : QStringLiteral("Search Playlist"));
}

void PlaylistSearchDialog::updateActionsEnabled() {
  const bool selected = resultsTable_->selectionModel()->hasSelection();
  showButton_->setEnabled(selected);
  queueButton_->setEnabled(selected);
  propertiesButton_->setEnabled(selected);
}

int PlaylistSearchDialog::selectedResult() const {
  const QModelIndexList rows = resultsTable_->selectionModel()->selectedRows();
  return rows.isEmpty() ? -1 : rows.first().row();
}

void PlaylistSearchDialog::emitForResult(
    int result, void (PlaylistSearchDialog::*signal)(int)) {
  if (!playlist_ || result < 0 ||
      result >= static_cast<int>(resultPks_.size())) {
    return;
  }
  const int pk = resultPks_[result];
  if (playlist_->containsPk(pk)) {
    emit(this->*signal)(playlist_->getIndexByPk(pk));
  }
}
