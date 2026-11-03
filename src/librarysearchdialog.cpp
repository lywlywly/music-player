#include "librarysearchdialog.h"
#include "ui_librarysearchdialog.h"

#include "monitoredlibrary.h"
#include <QHeaderView>

namespace {
const QString kNoWatchedFoldersMessage =
    QStringLiteral("No watched folders. Add one in Settings > Library.");
} // namespace

LibrarySearchDialog::LibrarySearchDialog(
    SongLibrary &songLibrary, const MonitoredLibrary &monitoredLibrary,
    GlobalColumnLayoutManager &columnLayoutManager, QWidget *parent)
    : QDialog(parent), ui(new Ui::LibrarySearchDialog),
      songLibrary_(songLibrary), monitoredLibrary_(monitoredLibrary) {
  ui->setupUi(this);
  setModal(true);
  ui->error_label->hide();

  resultsModel_ =
      new LibrarySearchResultsModel(songLibrary_, columnLayoutManager, this);
  ui->results_table->setModel(resultsModel_);
  ui->results_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui->results_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  ui->results_table->setSelectionMode(QAbstractItemView::SingleSelection);
  ui->results_table->setSortingEnabled(false);
  ui->results_table->horizontalHeader()->setStretchLastSection(true);
  ui->results_table->verticalHeader()->setVisible(false);

  connect(ui->search_button, &QPushButton::clicked, this,
          &LibrarySearchDialog::performSearch);
  connect(ui->expression_edit, &QLineEdit::returnPressed, this,
          &LibrarySearchDialog::performSearch);
  connect(ui->create_playlist_button, &QPushButton::clicked, this,
          &LibrarySearchDialog::createPlaylistFromCurrentResults);
  connect(ui->save_autoplaylist_button, &QPushButton::clicked, this, [this]() {
    emit saveAsAutoPlaylistRequested(currentExpression_);
    close();
  });
  if (monitoredLibrary_.folders().empty()) {
    showMessage(kNoWatchedFoldersMessage, false);
  }
}

LibrarySearchDialog::~LibrarySearchDialog() { delete ui; }

void LibrarySearchDialog::performSearch() {
  clearResults();
  if (monitoredLibrary_.folders().empty()) {
    showMessage(kNoWatchedFoldersMessage, false);
    return;
  }
  const QString expression = ui->expression_edit->text();
  const ExprParseResult parsed = parseSongFilter(songLibrary_, expression);
  if (!parsed.ok()) {
    showMessage(parsed.error.text(), true);
    return;
  }

  currentMatches_ =
      songLibrary_.search(*parsed.expr, monitoredLibrary_.songPks());
  currentExpression_ = expression;
  resultsModel_->setMatches(currentMatches_);
  ui->create_playlist_button->setEnabled(!currentMatches_.empty());
  ui->save_autoplaylist_button->setEnabled(true);
  showMessage(currentMatches_.size() == 1
                  ? QStringLiteral("1 match")
                  : QStringLiteral("%1 matches").arg(currentMatches_.size()),
              false);
  ui->results_table->resizeColumnsToContents();
}

void LibrarySearchDialog::createPlaylistFromCurrentResults() {
  if (currentMatches_.empty()) {
    return;
  }

  QList<int> songIds;
  songIds.reserve(static_cast<qsizetype>(currentMatches_.size()));
  for (int songId : currentMatches_) {
    songIds.push_back(songId);
  }
  emit createPlaylistRequested(songIds);
  close();
}

void LibrarySearchDialog::clearResults() {
  currentMatches_.clear();
  currentExpression_.clear();
  resultsModel_->clear();
  ui->create_playlist_button->setEnabled(false);
  ui->save_autoplaylist_button->setEnabled(false);
}

void LibrarySearchDialog::showMessage(const QString &message, bool error) {
  ui->error_label->setStyleSheet(error ? QStringLiteral("color: #b42318;")
                                       : QString());
  ui->error_label->setText(message);
  ui->error_label->show();
}
