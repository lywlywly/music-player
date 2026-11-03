#include "playlisttabs.h"
#include "autoplaylistdialog.h"
#include "playlistdialogs.h"
#include "playlisttableview.h"
#include "songdialogs.h"
#include "ui_playlisttabs.h"
#include <QActionGroup>
#include <QKeyEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QtGlobal>

PlaylistTabs::PlaylistTabs(QWidget *parent)
    : QWidget(parent), ui(new Ui::PlaylistTabs) {
  ui->setupUi(this);
}

PlaylistTabs::~PlaylistTabs() { delete ui; }

void PlaylistTabs::init(SongLibrary *s, PlaybackQueue *p, PlaybackManager *c,
                        QActionGroup *a, ColumnRegistry *registry,
                        GlobalColumnLayoutManager *layoutManager,
                        PlaylistCollection *collection) {
  songLibrary = s;
  collection_ = collection;
  playbackQueue_ = p;
  control = c;
  playbackOrderMenuActionGroup = a;
  Q_ASSERT(registry != nullptr);
  Q_ASSERT(layoutManager != nullptr);
  Q_ASSERT(collection_ != nullptr);
  columnRegistry_ = registry;
  columnLayoutManager_ = layoutManager;
  connect(collection_, &PlaylistCollection::playlistCreated, this,
          [this](int playlistId) {
            addPlaylistTab(collection_->name(playlistId), playlistId);
          });
  connect(collection_, &PlaylistCollection::playlistAboutToBeRemoved, this,
          &PlaylistTabs::removePlaylistTab);
  connect(collection_, &PlaylistCollection::playlistRenamed, this,
          [this](int playlistId, const QString &name) {
            const int index = tabIndexForPlaylistId(playlistId);
            if (index >= 0) {
              ui->tabWidget->setTabText(index, name);
            }
          });
  connect(collection_, &PlaylistCollection::autoPlaylistExpressionChanged, this,
          &PlaylistTabs::updateAutoPlaylistTab);
  setUpPlaylist();
  setUpPlaybackManager();
  setUpCurrentPlaylist();
}

void PlaylistTabs::setUpPlaylist() {
  // context menu
  playNextAction_ = playlistContextMenu.addAction("Play Next");
  playEndAction_ = playlistContextMenu.addAction("Add to Playback Queue");
  playlistContextMenu.addSeparator();
  removeFromPlaylistAction_ =
      playlistContextMenu.addAction("Remove from Playlist");
  clearPlaylistAction_ = playlistContextMenu.addAction("Clear Playlist");
  playlistContextMenu.addSeparator();
  openContainingFolderAction_ =
      playlistContextMenu.addAction("Open Containing Folder");
  propertiesAction_ = playlistContextMenu.addAction("Properties");
  propertiesAction_->setShortcuts(PlaylistTableView::propertiesShortcuts());
  propertiesAction_->setShortcutVisibleInContextMenu(true);
  // tabs
  QTabBar *tabBar = ui->tabWidget->tabBar();
  tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
  tabBar->setMovable(true);
  connect(tabBar, &QTabBar::customContextMenuRequested, this,
          &PlaylistTabs::onTabContextMenuRequested);
  connect(tabBar, &QTabBar::tabMoved, this,
          [this](int, int) { persistPlaylistTabOrder(); });
  connect(tabBar, &QTabBar::tabBarDoubleClicked, this,
          [this](int index) { beginInlineRename(index); });
  connect(ui->tabWidget, &QTabWidget::currentChanged, this,
          &PlaylistTabs::onTabChanged);

  tabRenameEditor_ = new QLineEdit(tabBar);
  tabRenameEditor_->hide();
  tabRenameEditor_->installEventFilter(this);
  connect(tabRenameEditor_, &QLineEdit::editingFinished, this,
          &PlaylistTabs::commitInlineRename);

  initializePlaylists();
}

void PlaylistTabs::navigateIndex(int row, Playlist *pl) {
  int tabIdx = -1;
  for (int i = 0; i < ui->tabWidget->count(); ++i) {
    Playlist *playlist = playlistForTabIndex(i);
    if (playlist == pl) {
      tabIdx = i;
      break;
    }
  }
  if (tabIdx >= 0)
    tabWidget()->setCurrentIndex(tabIdx);
  QModelIndex index = currentTableView->model()->index(row, 0);
  currentTableView->selectionModel()->select(
      index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
  currentTableView->setCurrentIndex(index);
  currentTableView->scrollTo(index);
}

int PlaylistTabs::firstSelectedRow() const {
  return currentTableView == nullptr ? -1
                                     : currentTableView->firstSelectedRow();
}

Playlist *PlaylistTabs::currentPlaylist() const { return currentPlaylist_; }

void PlaylistTabs::clearCurrentPlaylist() {
  if (currentPlaylist_ != nullptr) {
    currentPlaylist_->clear();
  }
}

void PlaylistTabs::createNewPlaylistTabFromSongIds(const QList<int> &songIds) {
  if (songIds.isEmpty()) {
    return;
  }

  collection_->createPlaylist(getNewPlaylistName(),
                              std::vector<int>(songIds.begin(), songIds.end()));
}

void PlaylistTabs::newAutoPlaylist(const QString &expression) {
  auto *dialog = new AutoPlaylistDialog(
      *songLibrary, collection_->monitoredLibrary(),
      getNewPlaylistName("New Autoplaylist"), expression, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &QDialog::accepted, this, [this, dialog]() {
    collection_->createAutoPlaylist(dialog->name(), dialog->expression());
  });
  dialog->open();
}

void PlaylistTabs::editAutoPlaylist(int playlistId) {
  if (!collection_->isAuto(playlistId)) {
    return;
  }
  auto *dialog = new AutoPlaylistDialog(
      *songLibrary, collection_->monitoredLibrary(),
      collection_->name(playlistId),
      collection_->autoPlaylistExpression(playlistId), this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &QDialog::accepted, this, [this, dialog, playlistId]() {
    collection_->renamePlaylist(playlistId, dialog->name());
    // An unchanged expression keeps the rows and their order.
    if (dialog->expression() !=
        collection_->autoPlaylistExpression(playlistId)) {
      collection_->setAutoPlaylistExpression(playlistId, dialog->expression());
    }
  });
  dialog->open();
}

void PlaylistTabs::setUpPlaybackManager() {
  connect(playbackOrderMenuActionGroup, &QActionGroup::triggered, this,
          [this](QAction *action) {
            control->setPolicy(static_cast<Policy>(action->data().toInt()));
          });
  connect(playNextAction_, &QAction::triggered, this, [this]() {
    QModelIndex index = playNextAction_->data().value<QModelIndex>();
    control->queueStart(index.row());
  });
  connect(playEndAction_, &QAction::triggered, this, [this]() {
    QModelIndex index = playEndAction_->data().value<QModelIndex>();
    control->queueEnd(index.row());
  });
  connect(removeFromPlaylistAction_, &QAction::triggered, this, [this]() {
    // The selected rows; right-clicking an unselected row selects it.
    std::vector<int> rows;
    for (const QModelIndex &index :
         currentTableView->selectionModel()->selectedRows()) {
      rows.push_back(index.row());
    }
    currentPlaylist_->removeSongs(std::move(rows));
  });
  connect(clearPlaylistAction_, &QAction::triggered, this,
          &PlaylistTabs::clearCurrentPlaylist);
  connect(openContainingFolderAction_, &QAction::triggered, this, [this]() {
    const int row =
        openContainingFolderAction_->data().value<QModelIndex>().row();
    if (isSongRow(row)) {
      revealSongFile(this, currentPlaylist_->getSongByIndex(row));
    }
  });
  connect(propertiesAction_, &QAction::triggered, this, [this]() {
    openPropertiesForRow(propertiesAction_->data().value<QModelIndex>().row());
  });
}

void PlaylistTabs::onCustomContextMenuRequested(const QPoint &pos) {
  QModelIndex index = currentTableView->indexAt(pos);
  const bool hasRow = index.isValid();
  const bool manual = currentPlaylist_->kind() == Playlist::Kind::Manual;
  playNextAction_->setEnabled(hasRow);
  playEndAction_->setEnabled(hasRow);
  removeFromPlaylistAction_->setVisible(manual);
  clearPlaylistAction_->setVisible(manual);
  removeFromPlaylistAction_->setEnabled(hasRow);
  openContainingFolderAction_->setEnabled(hasRow);
  propertiesAction_->setEnabled(hasRow);
  if (hasRow) {
    playNextAction_->setData(QVariant::fromValue(index));
    playEndAction_->setData(QVariant::fromValue(index));
    openContainingFolderAction_->setData(QVariant::fromValue(index));
    propertiesAction_->setData(QVariant::fromValue(index));
  }

  playlistContextMenu.exec(currentTableView->viewport()->mapToGlobal(pos));
}

void PlaylistTabs::onTabContextMenuRequested(const QPoint &pos) {
  QTabBar *tabBar = ui->tabWidget->tabBar();
  int index = tabBar->tabAt(pos);

  QMenu menu(this);
  QAction *addAction = menu.addAction("Add New Playlist");
  QAction *editAutoPlaylistAction = nullptr;
  QAction *refreshAction = nullptr;
  QAction *renameAction = nullptr;
  QAction *removeAction = nullptr;
  const int playlistId = playlistIdForTabIndex(index);

  if (index != -1) {
    if (collection_->isAuto(playlistId)) {
      editAutoPlaylistAction = menu.addAction("Edit Autoplaylist...");
    }
    refreshAction = menu.addAction("Refresh Playlist Metadata");
    renameAction = menu.addAction("Rename Playlist");
    removeAction = menu.addAction("Remove Tab");
    if (ui->tabWidget->count() <= 1) {
      removeAction->setEnabled(false);
    }
  }

  QAction *selected = menu.exec(tabBar->mapToGlobal(pos));

  if (selected == addAction) {
    collection_->createPlaylist(getNewPlaylistName());
  } else if (selected == editAutoPlaylistAction && index != -1) {
    editAutoPlaylist(playlistId);
  } else if (selected == refreshAction && index != -1) {
    if (Playlist *playlist = playlistForTabIndex(index)) {
      refreshMetadataWithProgress(this, *playlist);
    }
  } else if (selected == renameAction && index != -1) {
    beginInlineRename(index);
  } else if (selected == removeAction && index != -1) {
    removePlaylistTabByIndex(index);
  }
}

void PlaylistTabs::onTabChanged(int index) {
  if (index < 0) {
    currentPlaylist_ = nullptr;
    currentTableView = nullptr;
    emit currentPlaylistChanged(nullptr);
    return;
  }
  currentPlaylist_ = playlistForTabIndex(index);
  QWidget *tab = ui->tabWidget->widget(index);
  currentTableView = tab->findChild<PlaylistTableView *>();

  setUpCurrentPlaylist();
  QSettings settings;
  settings.setValue("playlist/last_opened_id", playlistIdForTabIndex(index));
  emit currentPlaylistChanged(currentPlaylist_);
}

void PlaylistTabs::setUpCurrentPlaylist() {
  Q_ASSERT(currentPlaylist_ != nullptr);
  control->setView(*currentPlaylist_);
  control->setPolicy(selectedPlaybackPolicy());
}

void PlaylistTabs::initializePlaylists() {
  const std::vector<int> playlistIds = collection_->playlistIdsInOrder();
  if (playlistIds.empty()) {
    qFatal("initializePlaylists: no playlist definitions");
  }

  {
    QSignalBlocker blocker(ui->tabWidget);
    while (ui->tabWidget->count() > 0) {
      QWidget *tab = ui->tabWidget->widget(0);
      ui->tabWidget->removeTab(0);
      delete tab;
    }

    for (int playlistId : playlistIds) {
      addPlaylistTab(collection_->name(playlistId), playlistId);
    }
    QSettings settings;
    const int lastOpenedPlaylistId =
        settings.value("playlist/last_opened_id", -1).toInt();
    int restoredIndex = 0;
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
      if (playlistIdForTabIndex(i) == lastOpenedPlaylistId) {
        restoredIndex = i;
        break;
      }
    }
    ui->tabWidget->setCurrentIndex(restoredIndex);
  }
  onTabChanged(ui->tabWidget->currentIndex());
  persistPlaylistTabOrder();
}

void PlaylistTabs::addPlaylistTab(const QString &playlistName, int playlistId) {
  Playlist *playlist = collection_->playlist(playlistId);
  Q_ASSERT(playlist != nullptr);
  QWidget *newTab = new QWidget;
  QGridLayout *layout = new QGridLayout(newTab);
  auto *table = new PlaylistTableView(*playlist, *playbackQueue_,
                                      *columnRegistry_, *columnLayoutManager_);
  layout->addWidget(table);
  connect(table, &QTableView::customContextMenuRequested, this,
          &PlaylistTabs::onCustomContextMenuRequested);
  connect(table, &QTableView::doubleClicked, this,
          &PlaylistTabs::doubleClicked);
  connect(table, &PlaylistTableView::propertiesRequested, this,
          &PlaylistTabs::openPropertiesForRow);
  qDebug() << "add playlist:" << playlistName;
  ui->tabWidget->addTab(newTab, playlistName);
  const int tabIndex = ui->tabWidget->indexOf(newTab);
  ui->tabWidget->tabBar()->setTabData(tabIndex, playlistId);
  updateAutoPlaylistTab(playlistId);
  ui->tabWidget->setCurrentWidget(newTab);
}

void PlaylistTabs::updateAutoPlaylistTab(int playlistId) {
  const int index = tabIndexForPlaylistId(playlistId);
  if (index < 0 || !collection_->isAuto(playlistId)) {
    return;
  }
  const QString error = collection_->autoPlaylistError(playlistId);
  ui->tabWidget->setTabIcon(
      index,
      style()->standardIcon(error.isEmpty() ? QStyle::SP_FileDialogContentsView
                                            : QStyle::SP_MessageBoxWarning));
  ui->tabWidget->setTabToolTip(
      index, error.isEmpty() ? collection_->autoPlaylistExpression(playlistId)
                             : error);
}

void PlaylistTabs::removePlaylistTab(int playlistId) {
  const int index = tabIndexForPlaylistId(playlistId);
  if (index < 0) {
    return;
  }
  QWidget *tab = ui->tabWidget->widget(index);
  ui->tabWidget->removeTab(index);
  delete tab;
}

QString PlaylistTabs::getNewPlaylistName(const QString &base) {
  if (findPlaylistIndex(base) < 0) {
    return base;
  }
  for (int i = 1;; i++) {
    const QString newName = QStringLiteral("%1 %2").arg(base).arg(i);
    if (findPlaylistIndex(newName) < 0) {
      return newName;
    }
  }
}

std::string PlaylistTabs::findPlaylistName(Playlist *pl) {
  for (int i = 0; i < ui->tabWidget->count(); ++i) {
    if (playlistForTabIndex(i) == pl) {
      return ui->tabWidget->tabText(i).toStdString();
    }
  }
  throw std::logic_error("Never");
}

int PlaylistTabs::findPlaylistIndex(QString s) {
  for (int i = 0; i < ui->tabWidget->count(); ++i) {
    QString text = ui->tabWidget->tabText(i);
    if (text == s)
      return i;
  }
  return -1;
}

bool PlaylistTabs::removePlaylistTabByIndex(int index) {
  if (index < 0 || index >= ui->tabWidget->count()) {
    return false;
  }
  return collection_->removePlaylist(playlistIdForTabIndex(index));
}

bool PlaylistTabs::renamePlaylistTabByIndex(int index, const QString &newName) {
  if (index < 0 || index >= ui->tabWidget->count()) {
    return false;
  }
  return collection_->renamePlaylist(playlistIdForTabIndex(index), newName);
}

void PlaylistTabs::beginInlineRename(int index) {
  if (index < 0 || index >= ui->tabWidget->count()) {
    return;
  }
  renamingTabIndex_ = index;
  QTabBar *tabBar = ui->tabWidget->tabBar();
  const QRect rect = tabBar->tabRect(index).adjusted(1, 1, -1, -1);
  tabRenameEditor_->setText(tabBar->tabText(index));
  tabRenameEditor_->setGeometry(rect);
  tabRenameEditor_->show();
  tabRenameEditor_->raise();
  tabRenameEditor_->setFocus();
  tabRenameEditor_->selectAll();
}

void PlaylistTabs::commitInlineRename() {
  if (!tabRenameEditor_->isVisible()) {
    return;
  }
  const int index = renamingTabIndex_;
  renamingTabIndex_ = -1;
  renamePlaylistTabByIndex(index, tabRenameEditor_->text());
  tabRenameEditor_->hide();
}

void PlaylistTabs::cancelInlineRename() {
  renamingTabIndex_ = -1;
  tabRenameEditor_->hide();
}

Policy PlaylistTabs::selectedPlaybackPolicy() const {
  return static_cast<Policy>(
      playbackOrderMenuActionGroup->checkedAction()->data().toInt());
}

QAction *PlaylistTabs::playNextAction() const { return playNextAction_; }

QAction *PlaylistTabs::playEndAction() const { return playEndAction_; }

QAction *PlaylistTabs::removeFromPlaylistAction() const {
  return removeFromPlaylistAction_;
}

QAction *PlaylistTabs::propertiesAction() const { return propertiesAction_; }

QTabWidget *PlaylistTabs::tabWidget() const { return ui->tabWidget; }

bool PlaylistTabs::isSongRow(int row) const {
  Q_ASSERT(currentPlaylist_ != nullptr);
  return row >= 0 && row < currentPlaylist_->songCount();
}

void PlaylistTabs::openPropertiesForRow(int row) {
  if (isSongRow(row)) {
    openSongProperties(this, currentPlaylist_->getPkByIndex(row), *songLibrary,
                       *columnRegistry_, *collection_);
  }
}

Playlist *PlaylistTabs::playlistForTabIndex(int index) {
  return collection_->playlist(playlistIdForTabIndex(index));
}

int PlaylistTabs::playlistIdForTabIndex(int index) const {
  if (index < 0 || index >= ui->tabWidget->count()) {
    return -1;
  }
  return ui->tabWidget->tabBar()->tabData(index).toInt();
}

int PlaylistTabs::tabIndexForPlaylistId(int playlistId) const {
  for (int i = 0; i < ui->tabWidget->count(); ++i) {
    if (playlistIdForTabIndex(i) == playlistId) {
      return i;
    }
  }
  return -1;
}

void PlaylistTabs::persistPlaylistTabOrder() {
  std::vector<int> playlistIdsInOrder;
  for (int i = 0; i < ui->tabWidget->count(); ++i) {
    playlistIdsInOrder.push_back(playlistIdForTabIndex(i));
  }
  if (!collection_->setPlaylistOrder(playlistIdsInOrder)) {
    qWarning() << "persistPlaylistTabOrder failed";
  }
}

bool PlaylistTabs::eventFilter(QObject *obj, QEvent *event) {
  if (obj == tabRenameEditor_ && event->type() == QEvent::KeyPress) {
    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
    if (keyEvent->key() == Qt::Key_Escape) {
      cancelInlineRename();
      return true;
    }
  }
  return QObject::eventFilter(obj, event);
}
