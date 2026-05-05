#include "playlisttabs.h"
#include "databasemanager.h"
#include "songpropertiesdialog.h"
#include "ui_playlisttabs.h"
#include <QActionGroup>
#include <QApplication>
#include <QHeaderView>
#include <QKeyEvent>
#include <QProgressDialog>
#include <QSettings>
#include <QSignalBlocker>
#include <QSqlError>
#include <QSqlQuery>
#include <QtGlobal>

PlaylistTabs::PlaylistTabs(QWidget *parent)
    : QWidget(parent), ui(new Ui::PlaylistTabs) {
  ui->setupUi(this);
}

PlaylistTabs::~PlaylistTabs() { delete ui; }

void PlaylistTabs::init(SongLibrary *s, PlaybackQueue *p, PlaybackManager *c,
                        QActionGroup *a, ColumnRegistry *registry,
                        GlobalColumnLayoutManager *layoutManager,
                        DatabaseManager *databaseManager) {
  songLibrary = s;
  databaseManager_ = databaseManager;
  playbackQueue_ = p;
  control = c;
  playbackOrderMenuActionGroup = a;
  Q_ASSERT(registry != nullptr);
  Q_ASSERT(layoutManager != nullptr);
  Q_ASSERT(databaseManager_ != nullptr);
  columnRegistry_ = registry;
  columnLayoutManager_ = layoutManager;
  setUpPlaylist();
  setUpPlaybackManager();
  setUpCurrentPlaylist();
}

void PlaylistTabs::setUpPlaylist() {
  // context menu
  playNextAction_ = playlistContextMenu.addAction("Play Next");
  playEndAction_ = playlistContextMenu.addAction("Add to Playback Queue");
  playlistContextMenu.addSeparator();
  clearPlaylistAction_ = playlistContextMenu.addAction("Clear Playlist");
  playlistContextMenu.addSeparator();
  propertiesAction_ = playlistContextMenu.addAction("Properties");
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

Playlist *PlaylistTabs::currentPlaylist() const { return currentPlaylist_; }

void PlaylistTabs::createNewPlaylistTabFromSongIds(const QList<int> &songIds) {
  if (songIds.isEmpty()) {
    return;
  }

  const QString playlistName = getNewPlaylistName();
  addNewPlaylistTab(playlistName);
  Playlist *playlist = currentPlaylist();
  for (int songId : songIds) {
    playlist->addSongByPk(songId);
  }
}

void PlaylistTabs::notifySongDataChangedInAllPlaylists(int songPk) {
  if (songPk < 0) {
    return;
  }
  for (auto &entry : playlistMap) {
    entry.second.emitSongDataChangedBySongPk(songPk);
  }
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
  connect(clearPlaylistAction_, &QAction::triggered, this,
          [this]() { currentPlaylist_->clear(); });
  connect(propertiesAction_, &QAction::triggered, this,
          &PlaylistTabs::openPropertiesForCurrentContextRow);
}

void PlaylistTabs::onCustomContextMenuRequested(const QPoint &pos) {
  QModelIndex index = currentTableView->indexAt(pos);
  const bool hasRow = index.isValid();
  playNextAction_->setEnabled(hasRow);
  playEndAction_->setEnabled(hasRow);
  propertiesAction_->setEnabled(hasRow);
  if (hasRow) {
    playNextAction_->setData(QVariant::fromValue(index));
    playEndAction_->setData(QVariant::fromValue(index));
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
    addNewPlaylistTab(getNewPlaylistName());
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
  const QList<PlaylistDefinition> definitions = loadPlaylistsInDisplayOrder();
  if (definitions.isEmpty()) {
    qFatal("initializePlaylists: no playlist definitions");
  }

  {
    QSignalBlocker blocker(ui->tabWidget);
    while (ui->tabWidget->count() > 0) {
      QWidget *tab = ui->tabWidget->widget(0);
      ui->tabWidget->removeTab(0);
      delete tab;
    }

    for (const PlaylistDefinition &definition : definitions) {
      addPlaylistTab(definition.name, definition.playlistId);
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
    const QList<QString> visibleIds = columnLayoutManager_->visibleColumnIds();
    if (idx < 0 || idx >= visibleIds.size()) {
      tbv->horizontalHeader()->setSortIndicatorShown(false);
      return;
    }

    const QString &columnId = visibleIds[idx];
    const ColumnDefinition *definition = columnRegistry_->findColumn(columnId);
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

void PlaylistTabs::createPlaylist(int playlistId, QTableView *tbv) {
  SongStore store{*songLibrary, *databaseManager_, playlistId};
  int lastPlayed = 1;
  if (!store.loadPlaylistState(lastPlayed)) {
    qFatal("createPlaylist: failed to load playlist state");
  }

  auto [it, inserted] = playlistMap.emplace(
      std::piecewise_construct, std::forward_as_tuple(playlistId),
      std::forward_as_tuple(std::move(store), *playbackQueue_, lastPlayed,
                            *columnLayoutManager_));
  if (!inserted) {
    qFatal("createPlaylist: duplicate playlist id");
  }

  Playlist &playlist = it->second;
  setUpTableView(&playlist, tbv);
  playlist.registerStatusUpdateCallback();
}

void PlaylistTabs::addPlaylistTab(const QString &playlistName, int playlistId) {
  QWidget *newTab = new QWidget;
  QGridLayout *layout = new QGridLayout(newTab);
  QTableView *tbv = new QTableView(this);
  layout->addWidget(tbv);
  createPlaylist(playlistId, tbv);
  qDebug() << "add playlist:" << playlistName;
  ui->tabWidget->addTab(newTab, playlistName);
  const int tabIndex = ui->tabWidget->indexOf(newTab);
  ui->tabWidget->tabBar()->setTabData(tabIndex, playlistId);
  ui->tabWidget->setCurrentWidget(newTab);
}

void PlaylistTabs::addNewPlaylistTab(const QString &playlistName) {
  const int playlistId =
      insertPlaylistRow(playlistName, ui->tabWidget->count());
  addPlaylistTab(playlistName, playlistId);
  persistPlaylistTabOrder();
}

int PlaylistTabs::insertPlaylistRow(const QString &playlistName, int tabOrder) {
  QSqlQuery q(databaseManager_->db());
  q.prepare(R"(
      INSERT INTO playlists(name, last_played, tab_order)
      VALUES(:name, 1, :tab_order)
  )");
  q.bindValue(":name", playlistName);
  q.bindValue(":tab_order", tabOrder);
  if (!q.exec()) {
    qFatal("insertPlaylistRow failed: %s",
           q.lastError().text().toUtf8().data());
  }
  return q.lastInsertId().toInt();
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
  if (ui->tabWidget->count() <= 1) {
    return false;
  }
  const int playlistId = playlistIdForTabIndex(index);
  auto it = playlistMap.find(playlistId);
  if (it == playlistMap.end()) {
    return false;
  }
  if (!deletePlaylistById(playlistId)) {
    return false;
  }
  QWidget *tab = ui->tabWidget->widget(index);
  ui->tabWidget->removeTab(index);
  delete tab;
  playlistMap.erase(it);
  persistPlaylistTabOrder();
  return true;
}

bool PlaylistTabs::renamePlaylistTabByIndex(int index, const QString &newName) {
  if (index < 0 || index >= ui->tabWidget->count()) {
    return false;
  }
  const QString trimmedName = newName.trimmed();
  if (trimmedName.isEmpty()) {
    return false;
  }
  const int playlistId = playlistIdForTabIndex(index);
  if (!updatePlaylistNameById(playlistId, trimmedName)) {
    return false;
  }
  ui->tabWidget->setTabText(index, trimmedName);
  return true;
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

QAction *PlaylistTabs::propertiesAction() const { return propertiesAction_; }

QTabWidget *PlaylistTabs::tabWidget() const { return ui->tabWidget; }

void PlaylistTabs::openPropertiesForCurrentContextRow() {
  Q_ASSERT(currentPlaylist_ != nullptr);
  Q_ASSERT(songLibrary != nullptr);
  Q_ASSERT(columnRegistry_ != nullptr);
  const QModelIndex index = propertiesAction_->data().value<QModelIndex>();
  if (!index.isValid()) {
    return;
  }
  const int row = index.row();
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

Playlist *PlaylistTabs::playlistForTabIndex(int index) {
  const int playlistId = playlistIdForTabIndex(index);
  auto it = playlistMap.find(playlistId);
  if (it == playlistMap.end()) {
    return nullptr;
  }
  return &it->second;
}

int PlaylistTabs::playlistIdForTabIndex(int index) const {
  if (index < 0 || index >= ui->tabWidget->count()) {
    return -1;
  }
  return ui->tabWidget->tabBar()->tabData(index).toInt();
}

void PlaylistTabs::persistPlaylistTabOrder() {
  QList<int> playlistIdsInOrder;
  for (int i = 0; i < ui->tabWidget->count(); ++i) {
    playlistIdsInOrder.push_back(playlistIdForTabIndex(i));
  }
  if (!updatePlaylistTabOrder(playlistIdsInOrder)) {
    qWarning() << "persistPlaylistTabOrder failed";
  }
}

QList<PlaylistTabs::PlaylistDefinition>
PlaylistTabs::loadPlaylistsInDisplayOrder() const {
  QSqlQuery q(databaseManager_->db());
  if (!q.exec(R"(
        SELECT playlist_id, name, tab_order
        FROM playlists
        ORDER BY tab_order ASC, playlist_id ASC
    )")) {
    qWarning() << "loadPlaylistsInDisplayOrder query failed:"
               << q.lastError().text();
    return {};
  }

  QList<PlaylistDefinition> definitions;
  while (q.next()) {
    definitions.push_back(PlaylistDefinition{.playlistId = q.value(0).toInt(),
                                             .name = q.value(1).toString(),
                                             .tabOrder = q.value(2).toInt()});
  }
  if (definitions.isEmpty()) {
    QSqlQuery ensureDefault(databaseManager_->db());
    if (!ensureDefault.exec(R"(
          INSERT INTO playlists(playlist_id, name, last_played, tab_order)
          VALUES(1, 'Default', 1, 0)
      )")) {
      qWarning() << "loadPlaylistsInDisplayOrder ensure default failed:"
                 << ensureDefault.lastError().text();
      return {};
    }
    definitions.push_back(
        PlaylistDefinition{.playlistId = 1, .name = "Default", .tabOrder = 0});
  }

  bool tabOrderValid = true;
  for (int i = 0; i < definitions.size(); ++i) {
    if (definitions[i].tabOrder != i) {
      tabOrderValid = false;
      break;
    }
  }
  if (tabOrderValid) {
    return definitions;
  }

  std::sort(definitions.begin(), definitions.end(),
            [](const PlaylistDefinition &a, const PlaylistDefinition &b) {
              return a.playlistId < b.playlistId;
            });
  QList<int> repairedOrder;
  repairedOrder.reserve(definitions.size());
  for (const PlaylistDefinition &definition : definitions) {
    repairedOrder.push_back(definition.playlistId);
  }
  if (!updatePlaylistTabOrder(repairedOrder)) {
    qWarning() << "loadPlaylistsInDisplayOrder failed to repair tab_order";
    return {};
  }
  for (int i = 0; i < definitions.size(); ++i) {
    definitions[i].tabOrder = i;
  }
  return definitions;
}

bool PlaylistTabs::updatePlaylistTabOrder(
    const QList<int> &playlistIdsInOrder) const {
  if (playlistIdsInOrder.empty()) {
    return true;
  }

  QSqlDatabase &db = databaseManager_->db();
  if (!db.transaction()) {
    qWarning() << "updatePlaylistTabOrder: failed to start transaction";
    return false;
  }

  QSqlQuery q(db);
  q.prepare(R"(
      UPDATE playlists
      SET tab_order=:tab_order
      WHERE playlist_id=:playlist_id
  )");
  for (int i = 0; i < playlistIdsInOrder.size(); ++i) {
    q.bindValue(":tab_order", i);
    q.bindValue(":playlist_id", playlistIdsInOrder[i]);
    if (!q.exec()) {
      db.rollback();
      qWarning() << "updatePlaylistTabOrder update failed:"
                 << q.lastError().text();
      return false;
    }
  }
  if (!db.commit()) {
    db.rollback();
    qWarning() << "updatePlaylistTabOrder commit failed:" << db.lastError();
    return false;
  }
  return true;
}

bool PlaylistTabs::deletePlaylistById(int playlistId) const {
  if (playlistId <= 0) {
    return false;
  }

  QSqlQuery q(databaseManager_->db());
  q.prepare(R"(
      DELETE FROM playlists
      WHERE playlist_id=:playlist_id
  )");
  q.bindValue(":playlist_id", playlistId);
  if (!q.exec()) {
    qWarning() << "deletePlaylistById failed:" << q.lastError().text();
    return false;
  }
  return true;
}

bool PlaylistTabs::updatePlaylistNameById(int playlistId,
                                          const QString &name) const {
  if (playlistId <= 0) {
    return false;
  }
  QSqlQuery q(databaseManager_->db());
  q.prepare(R"(
      UPDATE playlists
      SET name=:name
      WHERE playlist_id=:playlist_id
  )");
  q.bindValue(":name", name);
  q.bindValue(":playlist_id", playlistId);
  if (!q.exec()) {
    qWarning() << "updatePlaylistNameById failed:" << q.lastError().text();
    return false;
  }
  return true;
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
