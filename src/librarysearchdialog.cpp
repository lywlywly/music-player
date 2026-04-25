#include "librarysearchdialog.h"
#include "ui_librarysearchdialog.h"

#include <QHeaderView>

LibrarySearchDialog::LibrarySearchDialog(
    SongLibrary &songLibrary, GlobalColumnLayoutManager &columnLayoutManager,
    QWidget *parent)
    : QDialog(parent), ui(new Ui::LibrarySearchDialog),
      songLibrary_(songLibrary) {
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
}

LibrarySearchDialog::~LibrarySearchDialog() { delete ui; }

void LibrarySearchDialog::performSearch() {
  const ExprParseResult parsed =
      songLibrary_.parseLibraryExpression(ui->expression_edit->text());
  if (!parsed.ok()) {
    currentMatches_.clear();
    resultsModel_->clear();
    updateCreatePlaylistButtonState();
    showError(parsed.error);
    return;
  }

  currentMatches_ = songLibrary_.search(*parsed.expr);
  resultsModel_->setMatches(currentMatches_);
  updateCreatePlaylistButtonState();
  showMatchCount();
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

void LibrarySearchDialog::updateCreatePlaylistButtonState() {
  ui->create_playlist_button->setEnabled(!currentMatches_.empty());
}

void LibrarySearchDialog::showMatchCount() {
  ui->error_label->setStyleSheet(QString());
  if (currentMatches_.size() == 1) {
    ui->error_label->setText(QStringLiteral("1 match"));
  } else {
    ui->error_label->setText(
        QStringLiteral("%1 matches").arg(currentMatches_.size()));
  }
  ui->error_label->show();
}

void LibrarySearchDialog::showError(const ExprParseError &error) {
  ui->error_label->setStyleSheet(QStringLiteral("color: #b42318;"));
  if (error.position >= 0) {
    ui->error_label->setText(QStringLiteral("%1 (at %2)")
                                 .arg(error.message)
                                 .arg(error.position + 1));
  } else {
    ui->error_label->setText(error.message);
  }
  ui->error_label->show();
}
