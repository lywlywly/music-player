#include "playlisttabs.h"
#include "ui_playlisttabs.h"
#include <QActionGroup>
#include <QKeyEvent>

PlaylistTabs::PlaylistTabs(QWidget *parent)
    : QWidget(parent), ui(new Ui::PlaylistTabs) {
  ui->setupUi(this);
}

PlaylistTabs::~PlaylistTabs() { delete ui; }

void PlaylistTabs::init(SongLibrary *s, PlaybackQueue *p, PlaybackManager *c,
                        QActionGroup *a) {
  songLibrary = s;
  playbackQueue_ = p;
  control = c;
  playbackOrderMenuActionGroup = a;
  setUpPlaylist();
  setUpPlaybackManager();
  setUpCurrentPlaylist();
  setUpTableView(currentPlaylist_, currentTableView);
}

void PlaylistTabs::setUpPlaylist() {
  // context menu
  playNextAction_ = playlistContextMenu.addAction("Play Next");
  playEndAction_ = playlistContextMenu.addAction("Add to Playback Queue");
  // tabs
  ui->tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->tabWidget->tabBar(), &QTabBar::customContextMenuRequested, this,
          &PlaylistTabs::onTabContextMenuRequested);
  connect(ui->tabWidget, &QTabWidget::currentChanged, this,
          &PlaylistTabs::onTabChanged);
  ui->tabWidget->setTabText(0, "Default");
  // default playlist
  playlistMap.emplace(
      std::piecewise_construct, std::forward_as_tuple("Default"),
      std::forward_as_tuple(SongStore{*songLibrary}, *playbackQueue_));
  QWidget *tabWidget = ui->tabWidget->widget(0);
  currentTableView = tabWidget->findChild<QTableView *>();
  currentPlaylist_ = &playlistMap.at("Default");
  currentPlaylist_->registerStatusUpdateCallback();
}

void PlaylistTabs::navigateIndex(MSong song, int row, Playlist *pl) {
  int tabIdx = findPlaylistIndex(QString::fromUtf8(findPlaylistName(pl)));
  if (tabIdx >= 0)
    tabWidget()->setCurrentIndex(tabIdx);
  QModelIndex index = currentTableView->model()->index(row, 0);
  currentTableView->selectionModel()->select(
      index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
  currentTableView->setCurrentIndex(index);
  currentTableView->scrollTo(index);
}

Playlist *PlaylistTabs::currentPlaylist() const { return currentPlaylist_; }

void PlaylistTabs::setUpPlaybackManager() {
  connect(playbackOrderMenuActionGroup, &QActionGroup::triggered, this,
          [this](QAction *action) {
            QAction *checked = playbackOrderMenuActionGroup->checkedAction();
            QString selectedText = checked->text();
            control->setPolicy(string2Policy(selectedText));
          });
  connect(playNextAction_, &QAction::triggered, this, [&]() {
    QModelIndex index = playNextAction_->data().value<QModelIndex>();
    control->queueStart(index.row());
  });
  connect(playEndAction_, &QAction::triggered, this, [&]() {
    QModelIndex index = playNextAction_->data().value<QModelIndex>();
    control->queueEnd(index.row());
  });
}

void PlaylistTabs::onCustomContextMenuRequested(const QPoint &pos) {
  QModelIndex index = currentTableView->indexAt(pos);
  if (!index.isValid())
    return;

  playNextAction_->setData(QVariant::fromValue(index));
  playEndAction_->setData(QVariant::fromValue(index));

  playlistContextMenu.exec(currentTableView->viewport()->mapToGlobal(pos));
}

void PlaylistTabs::onTabContextMenuRequested(const QPoint &pos) {
  QTabBar *tabBar = ui->tabWidget->tabBar();
  int index = tabBar->tabAt(pos);

  QMenu menu(this);
  QAction *addAction = menu.addAction("Add New Playlist");
  QAction *removeAction = nullptr;

  if (index != -1)
    removeAction = menu.addAction("Remove Tab");

  QAction *selected = menu.exec(tabBar->mapToGlobal(pos));

  if (selected == addAction) {
    QWidget *newTab = new QWidget;
    QGridLayout *layout = new QGridLayout(newTab);
    QTableView *tbv = new QTableView(this);
    layout->addWidget(tbv);
    QString newPlaylistName = getNewPlaylistName();
    ui->tabWidget->addTab(newTab, newPlaylistName);
    playlistMap.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(std::string(newPlaylistName.toStdString())),
        std::forward_as_tuple(SongStore{*songLibrary}, *playbackQueue_));
    Playlist &pl = playlistMap.at(newPlaylistName.toStdString());

    setUpTableView(&pl, tbv);
    pl.registerStatusUpdateCallback();
  } else if (selected == removeAction) {
    std::string txt = ui->tabWidget->tabText(index).toStdString();
    ui->tabWidget->removeTab(index);
    playlistMap.erase(txt);
  }
}

void PlaylistTabs::onTabChanged(int index) {
  QString text = ui->tabWidget->tabText(index);
  qDebug() << "Switched to tab" << index << ":" << text;
  currentPlaylist_ = &playlistMap.at(text.toStdString());
  QWidget *tab = ui->tabWidget->widget(index);

  if (QTableView *tbv = tab->findChild<QTableView *>()) {
    currentTableView = tbv;
  }

  setUpCurrentPlaylist();
}

void PlaylistTabs::setUpCurrentPlaylist() {
  control->setView(currentPlaylist_);
  QAction *checked = playbackOrderMenuActionGroup->checkedAction();
  QString selectedText = checked->text();
  control->setPolicy(string2Policy(selectedText));
}

void PlaylistTabs::setUpTableView(Playlist *pl, QTableView *tbv) {
  tbv->setModel(pl);
  tbv->setSortingEnabled(true);
  tbv->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tbv->setSelectionBehavior(QAbstractItemView::SelectRows);
  tbv->installEventFilter(this);
  tbv->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tbv, &QTableView::customContextMenuRequested, this,
          &PlaylistTabs::onCustomContextMenuRequested);
  tbv->horizontalHeader()->setSortIndicatorShown(false);
  QObject::connect(tbv->horizontalHeader(), &QHeaderView::sectionClicked, this,
                   [=, this](int idx) {
                     tbv->horizontalHeader()->setSortIndicatorShown(true);
                     pl->sortByField(
                         fieldStringList[idx].toStdString(),
                         tbv->horizontalHeader()->sortIndicatorOrder());
                   });
  connect(tbv, &QTableView::doubleClicked, this, &PlaylistTabs::doubleClicked);
}

QString PlaylistTabs::getNewPlaylistName() {
  if (playlistMap.find("New Playlist") == playlistMap.end())
    return "New Playlist";
  for (int i = 1;; i++) {
    std::string newName = std::format("New Playlist {}", i);
    if (playlistMap.find(newName) == playlistMap.end())
      return QString::fromUtf8(newName);
  }
}

Policy PlaylistTabs::string2Policy(QString s) {
  if (s == "Default")
    return Policy::Sequential;
  else if (s == "Shuffle (tracks)")
    return Policy::Shuffle;
  throw std::logic_error("Never");
}

std::string PlaylistTabs::findPlaylistName(Playlist *pl) {
  for (const auto &[key, value] : playlistMap) {
    if (&value == pl)
      return key;
  }
  throw std::logic_error("Never");
}

int PlaylistTabs::findPlaylistIndex(QString s) {
  for (int i = 0; i < ui->tabWidget->count(); ++i) {
    QString text = ui->tabWidget->tabText(i);
    if (text == s)
      return i;
  }
  throw std::logic_error("Never");
}

QAction *PlaylistTabs::playNextAction() const { return playNextAction_; }

QAction *PlaylistTabs::playEndAction() const { return playEndAction_; }

QTabWidget *PlaylistTabs::tabWidget() const { return ui->tabWidget; }

bool PlaylistTabs::eventFilter(QObject *obj, QEvent *event) {
  if (obj == currentTableView && event->type() == QEvent::KeyPress) {
    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
    if (keyEvent->key() == Qt::Key_Backspace) {
      QModelIndex index = currentTableView->currentIndex();
      if (index.isValid()) {
        currentPlaylist_->removeSong(index.row());
      }
      return true;
    }
  }
  return QObject::eventFilter(obj, event);
}
