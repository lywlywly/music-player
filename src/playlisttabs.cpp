#include "playlisttabs.h"
#include "ui_playlisttabs.h"
#include <QActionGroup>
#include <QHeaderView>
#include <QKeyEvent>
#include <QSignalBlocker>

PlaylistTabs::PlaylistTabs(QWidget *parent)
    : QWidget(parent), ui(new Ui::PlaylistTabs),
      columnLayoutManager_(columnRegistry_, this) {
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
      std::forward_as_tuple(SongStore{*songLibrary, 1}, *playbackQueue_, 1,
                            columnLayoutManager_));
  QWidget *tabWidget = ui->tabWidget->widget(0);
  currentTableView = tabWidget->findChild<QTableView *>();
  currentPlaylist_ = &playlistMap.at("Default");
  currentPlaylist_->registerStatusUpdateCallback();
}

void PlaylistTabs::navigateIndex(MSong, int row, Playlist *pl) {
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
          [this](QAction *) {
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
    int newPid = playlistMap.size() + 1;
    playlistMap.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(std::string(newPlaylistName.toStdString())),
        std::forward_as_tuple(SongStore{*songLibrary, newPid}, *playbackQueue_,
                              newPid, columnLayoutManager_));
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
  tbv->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  tbv->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  tbv->setSortingEnabled(true);
  tbv->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tbv->setSelectionBehavior(QAbstractItemView::SelectRows);
  tbv->installEventFilter(this);
  tbv->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tbv, &QTableView::customContextMenuRequested, this,
          &PlaylistTabs::onCustomContextMenuRequested);

  QHeaderView *header = tbv->horizontalHeader();
  header->setSortIndicatorShown(false);
  header->setSectionsMovable(true);
  header->setContextMenuPolicy(Qt::CustomContextMenu);

  connect(header, &QHeaderView::sectionClicked, this, [this, tbv, pl](int idx) {
    const QList<QString> visibleIds = columnLayoutManager_.visibleColumnIds();
    if (idx < 0 || idx >= visibleIds.size()) {
      tbv->horizontalHeader()->setSortIndicatorShown(false);
      return;
    }

    const QString &columnId = visibleIds[idx];
    const ColumnDefinition *definition = columnRegistry_.findColumn(columnId);
    if (!definition || !definition->sortable) {
      tbv->horizontalHeader()->setSortIndicatorShown(false);
      return;
    }

    tbv->horizontalHeader()->setSortIndicatorShown(true);
    pl->sortByColumnId(columnId, tbv->horizontalHeader()->sortIndicatorOrder());
  });

  connect(header, &QHeaderView::sectionMoved, this, [this, tbv](int, int, int) {
    if (applyingColumnLayout_) {
      return;
    }
    persistVisibleOrder(tbv);
  });

  connect(header, &QHeaderView::sectionResized, this,
          [this](int logicalIndex, int, int newSize) {
            if (applyingColumnLayout_) {
              return;
            }
            const QList<QString> visibleIds =
                columnLayoutManager_.visibleColumnIds();
            if (logicalIndex < 0 || logicalIndex >= visibleIds.size()) {
              return;
            }
            const QString &columnId = visibleIds[logicalIndex];
            columnLayoutManager_.setColumnWidth(columnId, newSize);
          });

  connect(header, &QHeaderView::customContextMenuRequested, this,
          [this, tbv](const QPoint &pos) { showHeaderColumnsMenu(tbv, pos); });

  connect(&columnLayoutManager_, &GlobalColumnLayoutManager::layoutChanged,
          this, [this, tbv]() { applyLayoutToTableView(tbv); });

  connect(tbv, &QTableView::doubleClicked, this, &PlaylistTabs::doubleClicked);

  applyLayoutToTableView(tbv);
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

void PlaylistTabs::applyLayoutToTableView(QTableView *tbv) {
  QHeaderView *header = tbv->horizontalHeader();
  applyingColumnLayout_ = true;
  {
    QSignalBlocker blocker(header);
    const QList<QString> visibleIds = columnLayoutManager_.visibleColumnIds();
    for (int col = 0; col < visibleIds.size(); ++col) {
      const QString &columnId = visibleIds[col];
      tbv->setColumnWidth(col, columnLayoutManager_.columnWidth(columnId));
    }
    header->setSortIndicatorShown(false);
  }
  applyingColumnLayout_ = false;
}

void PlaylistTabs::persistVisibleOrder(QTableView *tbv) {
  const QList<QString> currentVisibleIds =
      columnLayoutManager_.visibleColumnIds();
  const int count = currentVisibleIds.size();
  if (count <= 0) {
    return;
  }

  QList<QString> visibleByVisualIndex;
  visibleByVisualIndex.resize(count);

  QHeaderView *header = tbv->horizontalHeader();
  for (int logical = 0; logical < count; ++logical) {
    const int visual = header->visualIndex(logical);
    if (visual < 0 || visual >= count) {
      continue;
    }
    visibleByVisualIndex[visual] = currentVisibleIds[logical];
  }

  QList<QString> newOrder;
  for (const QString &id : visibleByVisualIndex) {
    if (!id.isEmpty()) {
      newOrder.push_back(id);
    }
  }

  for (const QString &id : columnLayoutManager_.allOrderedColumnIds()) {
    if (!columnLayoutManager_.isColumnVisible(id)) {
      newOrder.push_back(id);
    }
  }

  columnLayoutManager_.setOrder(newOrder);
}

void PlaylistTabs::showHeaderColumnsMenu(QTableView *tbv, const QPoint &pos) {

  QMenu menu(this);
  for (const QString &id : columnLayoutManager_.allOrderedColumnIds()) {
    const ColumnDefinition *definition = columnRegistry_.findColumn(id);
    const QString title = definition ? definition->title : id;

    QAction *action = menu.addAction(title);
    action->setCheckable(true);
    action->setChecked(columnLayoutManager_.isColumnVisible(id));
    action->setData(id);
  }

  QAction *selected = menu.exec(tbv->horizontalHeader()->mapToGlobal(pos));
  if (!selected) {
    return;
  }

  const QString selectedId = selected->data().toString();
  const bool shouldBeVisible = selected->isChecked();

  if (!shouldBeVisible && columnLayoutManager_.visibleColumnIds().size() <= 1) {
    return;
  }

  columnLayoutManager_.setColumnVisible(selectedId, shouldBeVisible);
}

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
