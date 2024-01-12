#include "searchdialog.h"

#include "ui_searchdialog.h"

SearchDialog::SearchDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::SearchDialog) {
  ui->setupUi(this);

  setUpTable();

  loader = new SongLoader{this};
  connect(ui->pushButton, &QAbstractButton::clicked, this,
          &SearchDialog::query);
  connect(this, &SearchDialog::accepted, [this]() {
    emit newSongs(songs);
    // disconnect all connections with searchDialog, every time search dialog is
    // a new one
    disconnect();
  });
}

SearchDialog::~SearchDialog() { delete ui; }

void SearchDialog::query() {
  qDebug() << "pressed";
  model->clear();
  songs = loader->loadMetadataFromDB(ui->lineEdit->text());
  qDebug() << "size: " << songs.size();
  for (Song s : songs) {
    model->appendSong(s);
  }
}

void SearchDialog::setUpTable() {
  model = new SongTableModel(this);
  proxyModel = new MyProxyModel(this);
  proxyModel->setSourceModel(model);
  ui->tableView->setModel(proxyModel);
  ui->tableView->setSortingEnabled(true);
  ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
  tableHeader = new MyTableHeader(Qt::Orientation::Horizontal, this);
  ui->tableView->setHorizontalHeader(tableHeader);
  tableHeader->setSectionsClickable(true);
  QObject::connect(
      tableHeader, &QHeaderView::sectionClicked, this,
      [this](int logicalIndex) { proxyModel->toogleSortOrder(logicalIndex); });
}
