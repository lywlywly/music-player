#include "playlisttabs.h"
#include "filemanagerutils.h"
#include "playlisttablemodel.h"
#include "songpropertiesdialog.h"
#include "ui_playlisttabs.h"
#include <QActionGroup>
#include <QApplication>
#include <QFileInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QtGlobal>
#include <algorithm>

namespace {
constexpr const char *kOpenContainingFolderTitle = "Open Containing Folder";

QList<QKeySequence> openPropertiesShortcuts() {
#ifdef Q_OS_MACOS
  return {QKeySequence(Qt::CTRL | Qt::Key_I)};
#else
  return {QKeySequence(Qt::ALT | Qt::Key_Return),
          QKeySequence(Qt::ALT | Qt::Key_Enter)};
#endif
}

int selectedOrCurrentRow(QTableView *tableView) {
  const QModelIndexList selectedRows =
      tableView->selectionModel()->selectedRows();
  if (selectedRows.isEmpty()) {
    return tableView->currentIndex().row();
  }

  int row = selectedRows.front().row();
  for (const QModelIndex &index : selectedRows) {
    row = std::min(row, index.row());
  }
  return row;
}
} // namespace

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
  propertiesAction_->setShortcuts(openPropertiesShortcuts());
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
  if (currentTableView == nullptr ||
      currentTableView->selectionModel() == nullptr) {
    return -1;
  }

  const QModelIndexList selectedRows =
      currentTableView->selectionModel()->selectedRows();
  if (selectedRows.isEmpty()) {
    return -1;
  }

  int firstRow = selectedRows.front().row();
  for (const QModelIndex &index : selectedRows) {
    firstRow = std::min(firstRow, index.row());
  }
  return firstRow;
}

Playlist *PlaylistTabs::currentPlaylist() const { return currentPlaylist_; }

void PlaylistTabs::createNewPlaylistTabFromSongIds(const QList<int> &songIds) {
  if (songIds.isEmpty()) {
    return;
  }

  collection_->createPlaylist(getNewPlaylistName(),
                              std::vector<int>(songIds.begin(), songIds.end()));
}

void PlaylistTabs::notifySongDataChangedInAllPlaylists(int songPk) {
  collection_->notifySongChanged(songPk);
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
          [this]() { currentPlaylist_->clear(); });
  connect(openContainingFolderAction_, &QAction::triggered, this,
          &PlaylistTabs::openContainingFolderForCurrentContextRow);
  connect(propertiesAction_, &QAction::triggered, this,
          &PlaylistTabs::openPropertiesForCurrentContextRow);
}

void PlaylistTabs::onCustomContextMenuRequested(const QPoint &pos) {
  QModelIndex index = currentTableView->indexAt(pos);
  const bool hasRow = index.isValid();
  playNextAction_->setEnabled(hasRow);
  playEndAction_->setEnabled(hasRow);
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
  QAction *refreshAction = nullptr;
  QAction *renameAction = nullptr;
  QAction *removeAction = nullptr;

  if (index != -1) {
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
  } else if (selected == refreshAction && index != -1) {
    if (Playlist *playlist = playlistForTabIndex(index)) {
      refreshPlaylistMetadata(playlist);
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
    return;
  }
  currentPlaylist_ = playlistForTabIndex(index);
  QWidget *tab = ui->tabWidget->widget(index);
  currentTableView = tab->findChild<QTableView *>();

  setUpCurrentPlaylist();
  QSettings settings;
  settings.setValue("playlist/last_opened_id", playlistIdForTabIndex(index));
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

void PlaylistTabs::setUpTableView(Playlist *pl, QTableView *tbv) {
  auto *model =
      new PlaylistTableModel(*pl, *playbackQueue_, *columnLayoutManager_, tbv);
  tbv->setModel(model);
  tbv->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  tbv->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  tbv->setSortingEnabled(true);
  tbv->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tbv->setSelectionBehavior(QAbstractItemView::SelectRows);
  tbv->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tbv, &QTableView::customContextMenuRequested, this,
          &PlaylistTabs::onCustomContextMenuRequested);
  // QAction shortcuts on the popup context menu are metadata/display only here:
  // Qt does not dispatch them reliably from a transient QMenu. Register the
  // real shortcut on each table so Properties works while browsing rows.
  for (const QKeySequence &shortcutSequence : openPropertiesShortcuts()) {
    auto *shortcut = new QShortcut(shortcutSequence, tbv);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut, &QShortcut::activated, this,
            [this, tbv]() { openPropertiesForRow(selectedOrCurrentRow(tbv)); });
  }

  QHeaderView *header = tbv->horizontalHeader();
  header->setSortIndicatorShown(false);
  header->setSectionsMovable(true);
  header->setContextMenuPolicy(Qt::CustomContextMenu);

  connect(header, &QHeaderView::sectionClicked, this,
          [this, tbv, model](int idx) {
            const QList<QString> visibleIds =
                columnLayoutManager_->visibleColumnIds();
            if (idx < 0 || idx >= visibleIds.size()) {
              tbv->horizontalHeader()->setSortIndicatorShown(false);
              return;
            }

            const QString &columnId = visibleIds[idx];
            const ColumnDefinition *definition =
                columnRegistry_->findColumn(columnId);
            if (!definition || !definition->sortable) {
              tbv->horizontalHeader()->setSortIndicatorShown(false);
              return;
            }

            tbv->horizontalHeader()->setSortIndicatorShown(true);
            model->sortByColumnId(
                columnId, tbv->horizontalHeader()->sortIndicatorOrder());
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
                columnLayoutManager_->visibleColumnIds();
            if (logicalIndex < 0 || logicalIndex >= visibleIds.size()) {
              return;
            }
            const QString &columnId = visibleIds[logicalIndex];
            columnLayoutManager_->setColumnWidth(columnId, newSize);
          });

  connect(header, &QHeaderView::customContextMenuRequested, this,
          [this, tbv](const QPoint &pos) { showHeaderColumnsMenu(tbv, pos); });

  connect(columnLayoutManager_, &GlobalColumnLayoutManager::layoutChanged, tbv,
          [this, tbv]() { applyLayoutToTableView(tbv); });

  connect(tbv, &QTableView::doubleClicked, this, &PlaylistTabs::doubleClicked);

  applyLayoutToTableView(tbv);
}

void PlaylistTabs::addPlaylistTab(const QString &playlistName, int playlistId) {
  Playlist *playlist = collection_->playlist(playlistId);
  Q_ASSERT(playlist != nullptr);
  QWidget *newTab = new QWidget;
  QGridLayout *layout = new QGridLayout(newTab);
  QTableView *tbv = new QTableView(this);
  layout->addWidget(tbv);
  setUpTableView(playlist, tbv);
  qDebug() << "add playlist:" << playlistName;
  ui->tabWidget->addTab(newTab, playlistName);
  const int tabIndex = ui->tabWidget->indexOf(newTab);
  ui->tabWidget->tabBar()->setTabData(tabIndex, playlistId);
  ui->tabWidget->setCurrentWidget(newTab);
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

QString PlaylistTabs::getNewPlaylistName() {
  if (findPlaylistIndex("New Playlist") < 0) {
    return "New Playlist";
  }
  for (int i = 1;; i++) {
    std::string newName = std::format("New Playlist {}", i);
    if (findPlaylistIndex(QString::fromUtf8(newName)) < 0) {
      return QString::fromUtf8(newName);
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

void PlaylistTabs::openPropertiesForCurrentContextRow() {
  const QModelIndex index = propertiesAction_->data().value<QModelIndex>();
  openPropertiesForRow(index.row());
}

void PlaylistTabs::openPropertiesForRow(int row) {
  Q_ASSERT(currentPlaylist_ != nullptr);
  Q_ASSERT(songLibrary != nullptr);
  Q_ASSERT(columnRegistry_ != nullptr);
  if (row < 0 || row >= currentPlaylist_->songCount()) {
    return;
  }
  const MSong &song = currentPlaylist_->getSongByIndex(row);
  const int songPk = currentPlaylist_->getPkByIndex(row);
  auto filepathIt = song.find("filepath");
  if (filepathIt == song.end() || filepathIt->second.text.empty()) {
    return;
  }

  std::unordered_map<std::string, std::string> remainingFields;
  const MSong &refreshedSong = songLibrary->refreshSongFromFile(
      filepathIt->second.text, &remainingFields);

  auto *dialog = new SongPropertiesDialog(
      songPk, filepathIt->second.text, *songLibrary, refreshedSong,
      remainingFields, *columnRegistry_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setModal(true);
  connect(dialog, &SongPropertiesDialog::songUpdated, this,
          [this](int updatedSongPk) {
            notifySongDataChangedInAllPlaylists(updatedSongPk);
          });
  dialog->show();
}

void PlaylistTabs::openContainingFolderForCurrentContextRow() {
  const QModelIndex index =
      openContainingFolderAction_->data().value<QModelIndex>();
  openContainingFolderForRow(index.row());
}

void PlaylistTabs::openContainingFolderForRow(int row) {
  Q_ASSERT(currentPlaylist_ != nullptr);
  if (row < 0 || row >= currentPlaylist_->songCount()) {
    return;
  }

  const MSong &song = currentPlaylist_->getSongByIndex(row);
  const auto filepathIt = song.find("filepath");
  const QString filepath =
      filepathIt == song.end()
          ? QString()
          : QString::fromStdString(filepathIt->second.text).trimmed();
  const QFileInfo fileInfo(filepath);
  if (filepath.isEmpty() || !fileInfo.exists() || !fileInfo.isFile()) {
    QMessageBox::warning(
        this, kOpenContainingFolderTitle,
        QStringLiteral("The selected file does not exist:\n%1").arg(filepath));
    return;
  }

  if (!FileManagerUtils::revealFile(fileInfo.absoluteFilePath())) {
    QMessageBox::warning(
        this, kOpenContainingFolderTitle,
        QStringLiteral("Could not open the containing folder for:\n%1")
            .arg(fileInfo.absoluteFilePath()));
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

void PlaylistTabs::applyLayoutToTableView(QTableView *tbv) {
  QHeaderView *header = tbv->horizontalHeader();
  applyingColumnLayout_ = true;
  {
    QSignalBlocker blocker(header);
    const QList<QString> visibleIds = columnLayoutManager_->visibleColumnIds();
    for (int col = 0; col < visibleIds.size(); ++col) {
      const QString &columnId = visibleIds[col];
      tbv->setColumnWidth(col, columnLayoutManager_->columnWidth(columnId));
    }
    header->setSortIndicatorShown(false);
  }
  applyingColumnLayout_ = false;
}

void PlaylistTabs::persistVisibleOrder(QTableView *tbv) {
  const QList<QString> currentVisibleIds =
      columnLayoutManager_->visibleColumnIds();
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

  for (const QString &id : columnLayoutManager_->allOrderedColumnIds()) {
    if (!columnLayoutManager_->isColumnVisible(id)) {
      newOrder.push_back(id);
    }
  }

  columnLayoutManager_->setOrder(newOrder);
}

void PlaylistTabs::showHeaderColumnsMenu(QTableView *tbv, const QPoint &pos) {

  QMenu menu(this);
  for (const QString &id : columnLayoutManager_->allOrderedColumnIds()) {
    const ColumnDefinition *definition = columnRegistry_->findColumn(id);
    const QString title = definition ? definition->title : id;

    QAction *action = menu.addAction(title);
    action->setCheckable(true);
    action->setChecked(columnLayoutManager_->isColumnVisible(id));
    action->setData(id);
  }

  QAction *selected = menu.exec(tbv->horizontalHeader()->mapToGlobal(pos));
  if (!selected) {
    return;
  }

  const QString selectedId = selected->data().toString();
  const bool shouldBeVisible = selected->isChecked();

  if (!shouldBeVisible &&
      columnLayoutManager_->visibleColumnIds().size() <= 1) {
    return;
  }

  columnLayoutManager_->setColumnVisible(selectedId, shouldBeVisible);
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

void PlaylistTabs::refreshPlaylistMetadata(Playlist *playlist) {
  QProgressDialog progressDialog("Refreshing playlist metadata...", QString{},
                                 0, playlist->songCount(), this);
  progressDialog.setWindowModality(Qt::WindowModal);
  progressDialog.setCancelButton(nullptr);
  progressDialog.setMinimumDuration(0);
  progressDialog.setValue(0);
  playlist->refreshMetadataFromFiles([&](int current, int total) {
    progressDialog.setMaximum(total);
    progressDialog.setValue(current);
    QApplication::processEvents();
  });
  progressDialog.setValue(progressDialog.maximum());
}
