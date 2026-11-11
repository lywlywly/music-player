#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTabBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <QtGlobal>
#include <unordered_map>

#include "../autoplaylistdialog.h"
#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../globalcolumnlayoutmanager.h"
#include "../monitoredlibrary.h"
#include "../playbackmanager.h"
#include "../playbackqueue.h"
#include "../playlistcollection.h"
#include "../playlisttableview.h"
#include "../playlisttabs.h"
#include "../songlibrary.h"
#include "../songparser.h"
#include "../songpropertiesdialog.h"
#include "../utils.h"

namespace {
MSong makeSong(const QString &title, const QString &artist, const QString &path,
               const QString &tracknumber = {}) {
  MSong song;
  song.insert_or_assign("title", FieldValue(title.toStdString(), "title"));
  song.insert_or_assign("artist", FieldValue(artist.toStdString(), "artist"));
  song.insert_or_assign("album", FieldValue("Album", "album"));
  song.insert_or_assign("discnumber", FieldValue("1", "discnumber"));
  if (!tracknumber.isEmpty()) {
    song.insert_or_assign("tracknumber",
                          FieldValue(tracknumber.toStdString(), "tracknumber"));
  }
  song.insert_or_assign("date", FieldValue("2024-01-01", "date"));
  song.insert_or_assign("genre", FieldValue("genre", "genre"));
  song.insert_or_assign("filepath", FieldValue(path.toStdString(), "filepath"));
  const std::string identity =
      util::normalizedText(title).toStdString() + "|" +
      util::normalizedText(artist).toStdString() + "|" +
      util::normalizedText(QStringLiteral("Album")).toStdString();
  song.insert_or_assign("song_identity_key",
                        FieldValue(identity, "song_identity_key"));
  return song;
}

QActionGroup *makePlaybackOrderGroup(QObject *parent = nullptr) {
  auto *group = new QActionGroup(parent);
  group->setExclusive(true);

  QAction *defaultAction = new QAction("Default", group);
  defaultAction->setCheckable(true);
  defaultAction->setData(Policy::Sequential);
  defaultAction->setChecked(true);
  group->addAction(defaultAction);

  QAction *shuffleAction = new QAction("Shuffle (tracks)", group);
  shuffleAction->setCheckable(true);
  shuffleAction->setData(Policy::Shuffle);
  group->addAction(shuffleAction);
  return group;
}

int insertPlaylistRow(QSqlDatabase &db, const QString &name, int tabOrder) {
  QSqlQuery q(db);
  q.prepare(R"(
      INSERT INTO playlists(name, last_played, tab_order)
      VALUES(:name, 1, :tab_order)
  )");
  q.bindValue(":name", name);
  q.bindValue(":tab_order", tabOrder);
  if (!q.exec()) {
    return -1;
  }
  return q.lastInsertId().toInt();
}

bool updatePlaylistTabOrder(QSqlDatabase &db, const QList<int> &playlistIds) {
  if (playlistIds.isEmpty()) {
    return true;
  }
  if (!db.transaction()) {
    return false;
  }
  QSqlQuery q(db);
  q.prepare(R"(
      UPDATE playlists
      SET tab_order=:tab_order
      WHERE playlist_id=:playlist_id
  )");
  for (int i = 0; i < playlistIds.size(); ++i) {
    q.bindValue(":tab_order", i);
    q.bindValue(":playlist_id", playlistIds[i]);
    if (!q.exec()) {
      db.rollback();
      return false;
    }
  }
  if (!db.commit()) {
    db.rollback();
    return false;
  }
  return true;
}

} // namespace

class TestPlaylistTabs : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void init_createsDefaultPlaylistAndActions();
  void init_persistsDefaultPlaylistMetadataRow();
  void helperMethods_mapAndFindPlaylist();
  void navigateIndex_selectsTargetRow();
  void tableDoubleClick_isForwarded();
  void customContextMenu_bindsRowIntoQueueActions();
  void removeFromPlaylistAction_removesSelectedRows();
  void propertiesAction_showsPropertiesShortcuts();
  void propertiesShortcut_opensDialogForSongRow();
  void propertiesDialog_showsRefreshedAndRemainingFields();
  void propertiesDialog_editSave_writesDirtyFieldAndEmitsSongUpdated();
  void propertiesDialog_cancel_discardsBufferedEdits();
  void propertiesDialog_removeField_removesNonBuiltInOnSave();
  void propertiesDialog_removeField_blocksBuiltIn();
  void propertiesDialog_addField_addsAndSavesTag();
  void createNewPlaylistTabFromSongIds_createsTabAndCopiesSongs();
  void createNewPlaylistTabFromSongIds_emptyNoop();
  void startupRestore_loadsAllPlaylistsInSavedOrder();
  void startupRestore_restoresLastOpenedPlaylistTab();
  void startupRestore_recoversCorruptedTabOrder();
  void removeTab_deletesNonDefaultPlaylist();
  void removeTab_rejectsWhenSingleTab();
  void removeTab_invalidIndexRejected();
  void removeTab_allowsRemovingDefaultWhenMultipleTabs();
  void tabMove_persistsOrderInDb();
  void renameTab_updatesTabTextAndDb();
  void autoPlaylistTab_showsIconAndErrorTooltip();
  void autoPlaylistMenus_leaveOutWritesAndOfferEdit();
  void newAutoPlaylist_dialogChecksExpressionAndCreates();
  void editAutoPlaylist_renamesAndSetsExpression();
  void selectedSongChanged_followsSelectionAndTabs();
  void selectedSongChanged_followsRemovalAndSort();
  void selectedSongChanged_followsAutoPlaylistReset();

private:
  void recreateTabs();
  // Creates empty files with these names, watches their folder and waits for
  // the scan. The monitored library parses a file as a song titled by its
  // base name.
  void watchSongs(const QStringList &fileNames);
  // Closes the popup menu shown next, after recording its action texts.
  void closeNextPopup(QStringList *actionTexts);

  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  GlobalColumnLayoutManager *layout_ = nullptr;
  PlaybackQueue *queue_ = nullptr;
  PlaybackManager *manager_ = nullptr;
  QActionGroup *playbackOrderGroup_ = nullptr;
  MonitoredLibrary *monitored_ = nullptr;
  PlaylistCollection *collection_ = nullptr;
  PlaylistTabs *tabs_ = nullptr;
  QTemporaryDir *songDir_ = nullptr;
  QString connectionName_;
};

void TestPlaylistTabs::init() {
  QCoreApplication::setOrganizationName("music-player-tests");
  QCoreApplication::setApplicationName("playlisttabs-tests");
  QSettings settings;
  settings.clear();

  connectionName_ =
      QStringLiteral("test_playlisttabs_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(
      *registry_, *databaseManager_,
      [](const std::string &path, const ColumnRegistry &registry,
         std::unordered_map<std::string, std::string> *remainingFields)
          -> MSong {
        if (remainingFields != nullptr) {
          (*remainingFields)["remaining_zeta"] = "z-value";
          (*remainingFields)["remaining_alpha"] = "a-value";
        }
        MSong parsed = makeSong("ParsedTitle", "ParsedArtist",
                                QString::fromStdString(path), "1");
        if (registry.hasColumn("attr:lyrics")) {
          parsed.insert_or_assign("attr:lyrics",
                                  FieldValue("old-lyrics", "attr:lyrics"));
        }
        return parsed;
      });
  layout_ = new GlobalColumnLayoutManager(*registry_);
  queue_ = new PlaybackQueue();
  manager_ = new PlaybackManager(*queue_);
  playbackOrderGroup_ = makePlaybackOrderGroup();
  recreateTabs();
}

void TestPlaylistTabs::recreateTabs() {
  delete tabs_;
  delete collection_;
  delete monitored_;
  monitored_ =
      new MonitoredLibrary(*library_, *registry_, *databaseManager_,
                           [](const std::string &path, const ColumnRegistry &) {
                             const QString qpath = QString::fromStdString(path);
                             return std::optional<MSong>(makeSong(
                                 QFileInfo(qpath).baseName(), "Artist", qpath));
                           });
  monitored_->load();
  collection_ =
      new PlaylistCollection(*library_, *databaseManager_, *monitored_);
  collection_->loadAll();
  tabs_ = new PlaylistTabs();
  tabs_->init(library_, queue_, manager_, playbackOrderGroup_, registry_,
              layout_, collection_);
}

void TestPlaylistTabs::cleanup() {
  QTRY_VERIFY(!monitored_->isScanning());
  delete songDir_;
  songDir_ = nullptr;
  delete tabs_;
  tabs_ = nullptr;
  delete collection_;
  collection_ = nullptr;
  delete monitored_;
  monitored_ = nullptr;
  delete playbackOrderGroup_;
  playbackOrderGroup_ = nullptr;
  delete manager_;
  manager_ = nullptr;
  delete queue_;
  queue_ = nullptr;
  delete layout_;
  layout_ = nullptr;
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
}

void TestPlaylistTabs::init_createsDefaultPlaylistAndActions() {
  QVERIFY(tabs_->currentPlaylist() != nullptr);
  QVERIFY(tabs_->playNextAction() != nullptr);
  QVERIFY(tabs_->playEndAction() != nullptr);
  QCOMPARE(tabs_->tabWidget()->count(), 1);
  QCOMPARE(tabs_->tabWidget()->tabText(0), QString("Default"));
}

void TestPlaylistTabs::init_persistsDefaultPlaylistMetadataRow() {
  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("SELECT name, tab_order FROM playlists WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("Default"));
  QCOMPARE(q.value(1).toInt(), 0);
}

void TestPlaylistTabs::helperMethods_mapAndFindPlaylist() {
  QAction *defaultAction = playbackOrderGroup_->actions().at(0);
  QAction *shuffleAction = playbackOrderGroup_->actions().at(1);
  QCOMPARE(defaultAction->data().toInt(), static_cast<int>(Policy::Sequential));
  QCOMPARE(shuffleAction->data().toInt(), static_cast<int>(Policy::Shuffle));

  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  QCOMPARE(QString::fromStdString(tabs_->findPlaylistName(pl)),
           QString("Default"));
  QCOMPARE(tabs_->findPlaylistIndex("Default"), 0);
}

void TestPlaylistTabs::navigateIndex_selectsTargetRow() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-nav-a.mp3", "1"));
  pl->addSong(makeSong("B", "Artist", "/tmp/pt-nav-b.mp3", "2"));

  tabs_->navigateIndex(1, pl);

  QTableView *table =
      tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);
  QCOMPARE(table->currentIndex().row(), 1);
  QVERIFY(table->selectionModel()->isRowSelected(1, QModelIndex()));
}

void TestPlaylistTabs::tableDoubleClick_isForwarded() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-double-click.mp3", "1"));
  QTableView *table =
      tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);
  QSignalSpy spy(tabs_, &PlaylistTabs::doubleClicked);

  emit table->doubleClicked(table->model()->index(0, 0));

  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).value<QModelIndex>().row(), 0);
}

void TestPlaylistTabs::customContextMenu_bindsRowIntoQueueActions() {
  if (QGuiApplication::primaryScreen() == nullptr) {
    QSKIP("No screen available for popup menu test");
  }

  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-menu-a.mp3", "1"));

  QTableView *table =
      tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);
  const QModelIndex row0 = table->model()->index(0, 0);
  QVERIFY(row0.isValid());
  const QPoint validPos = table->visualRect(row0).center();

  QTimer::singleShot(0, []() {
    if (QWidget *popup = QApplication::activePopupWidget()) {
      popup->close();
    }
  });
  emit table->customContextMenuRequested(validPos);
  QVERIFY(tabs_->playNextAction()->isEnabled());
  QVERIFY(tabs_->playEndAction()->isEnabled());
  QVERIFY(tabs_->removeFromPlaylistAction()->isEnabled());
  QVERIFY(tabs_->propertiesAction()->isEnabled());
  QCOMPARE(tabs_->playNextAction()->data().value<QModelIndex>().row(), 0);
  QCOMPARE(tabs_->playEndAction()->data().value<QModelIndex>().row(), 0);
  QCOMPARE(tabs_->propertiesAction()->data().value<QModelIndex>().row(), 0);

  QMenu *contextMenu = nullptr;
  for (QObject *object : tabs_->propertiesAction()->associatedObjects()) {
    contextMenu = qobject_cast<QMenu *>(object);
    if (contextMenu != nullptr) {
      break;
    }
  }
  QVERIFY(contextMenu != nullptr);
  QCOMPARE(contextMenu->actions().last(), tabs_->propertiesAction());

  QTimer::singleShot(0, []() {
    if (QWidget *popup = QApplication::activePopupWidget()) {
      popup->close();
    }
  });
  tabs_->onCustomContextMenuRequested(QPoint(-100, -100));
  QVERIFY(!tabs_->playNextAction()->isEnabled());
  QVERIFY(!tabs_->playEndAction()->isEnabled());
  QVERIFY(!tabs_->removeFromPlaylistAction()->isEnabled());
  QVERIFY(!tabs_->propertiesAction()->isEnabled());
}

void TestPlaylistTabs::removeFromPlaylistAction_removesSelectedRows() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-remove-a.mp3", "1"));
  pl->addSong(makeSong("B", "Artist", "/tmp/pt-remove-b.mp3", "2"));
  pl->addSong(makeSong("C", "Artist", "/tmp/pt-remove-c.mp3", "3"));
  QTableView *table =
      tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);
  table->selectRow(0);
  table->selectionModel()->select(table->model()->index(2, 0),
                                  QItemSelectionModel::Select |
                                      QItemSelectionModel::Rows);

  tabs_->removeFromPlaylistAction()->trigger();

  QCOMPARE(pl->songCount(), 1);
  QCOMPARE(pl->getSongByIndex(0).at("title").text, std::string("B"));
}

void TestPlaylistTabs::propertiesAction_showsPropertiesShortcuts() {
  QCOMPARE(tabs_->propertiesAction()->shortcuts(),
           PlaylistTableView::propertiesShortcuts());
  QVERIFY(tabs_->propertiesAction()->isShortcutVisibleInContextMenu());
}

void TestPlaylistTabs::propertiesShortcut_opensDialogForSongRow() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-props-shortcut.mp3", "1"));
  auto *table =
      tabs_->tabWidget()->currentWidget()->findChild<PlaylistTableView *>();
  QVERIFY(table != nullptr);

  emit table->propertiesRequested(-1);
  QVERIFY(tabs_->findChild<SongPropertiesDialog *>() == nullptr);

  emit table->propertiesRequested(0);
  auto *dialog = tabs_->findChild<SongPropertiesDialog *>();
  QVERIFY(dialog != nullptr);
  delete dialog;
}

void TestPlaylistTabs::propertiesDialog_showsRefreshedAndRemainingFields() {
  registry_->addOrUpdateDynamicColumn(
      {.id = "computed:era",
       .title = "Era",
       .sortable = true,
       .visibleByDefault = true,
       .defaultWidth = 140},
      {.id = "computed:era",
       .source = ColumnSource::Computed,
       .valueType = ValueType::Text,
       .displayKind = DisplayKind::Raw,
       .expression = "IF artist IS ParsedArtist THEN classic ELSE modern",
       .searchable = true,
       .writable = false});

  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("OldTitle", "OldArtist", "/tmp/pt-props.mp3", "1"));

  QTableView *table =
      tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);
  const QModelIndex row0 = table->model()->index(0, 0);
  QVERIFY(row0.isValid());
  const QPoint validPos = table->visualRect(row0).center();

  QTimer::singleShot(0, []() {
    if (QWidget *popup = QApplication::activePopupWidget()) {
      popup->close();
    }
  });
  tabs_->onCustomContextMenuRequested(validPos);
  QVERIFY(tabs_->propertiesAction()->isEnabled());
  tabs_->propertiesAction()->trigger();
  QTest::qWait(0);

  SongPropertiesDialog *dialog = nullptr;
  for (QWidget *widget : QApplication::topLevelWidgets()) {
    dialog = qobject_cast<SongPropertiesDialog *>(widget);
    if (dialog != nullptr) {
      break;
    }
  }
  QVERIFY(dialog != nullptr);
  QVERIFY(dialog->isModal());

  QTableView *propertiesTable =
      dialog->findChild<QTableView *>("properties_table");
  QVERIFY(propertiesTable != nullptr);
  QAbstractItemModel *model = propertiesTable->model();
  QVERIFY(model != nullptr);

  QCOMPARE(model->headerData(0, Qt::Horizontal, Qt::DisplayRole).toString(),
           QString("Field"));
  QCOMPARE(model->headerData(1, Qt::Horizontal, Qt::DisplayRole).toString(),
           QString("Value"));
  QCOMPARE(model->headerData(2, Qt::Horizontal, Qt::DisplayRole).toString(),
           QString("Type"));

  int computedRow = -1;
  int remainingAlphaRow = -1;
  int remainingZetaRow = -1;
  for (int row = 0; row < model->rowCount(); ++row) {
    const QString fieldText =
        model->data(model->index(row, 0), Qt::DisplayRole).toString();
    if (fieldText.contains("(era)")) {
      computedRow = row;
    } else if (fieldText == "remaining_alpha") {
      remainingAlphaRow = row;
    } else if (fieldText == "remaining_zeta") {
      remainingZetaRow = row;
    }
  }

  QVERIFY(computedRow >= 0);
  QVERIFY(remainingAlphaRow >= 0);
  QVERIFY(remainingZetaRow >= 0);
  QVERIFY(remainingAlphaRow > computedRow);
  QVERIFY(remainingZetaRow > computedRow);
  QVERIFY(remainingAlphaRow < remainingZetaRow);

  QCOMPARE(model->data(model->index(remainingAlphaRow, 2), Qt::DisplayRole)
               .toString(),
           QString("Text"));
  QCOMPARE(model->data(model->index(remainingZetaRow, 1), Qt::DisplayRole)
               .toString(),
           QString("z-value"));

  dialog->close();
  delete dialog;
}

void TestPlaylistTabs::
    propertiesDialog_editSave_writesDirtyFieldAndEmitsSongUpdated() {
  registry_->addOrUpdateDynamicColumn({.id = "attr:lyrics",
                                       .title = "Lyrics",
                                       .sortable = true,
                                       .visibleByDefault = true,
                                       .defaultWidth = 180},
                                      {.id = "attr:lyrics",
                                       .source = ColumnSource::SongAttribute,
                                       .valueType = ValueType::Text,
                                       .displayKind = DisplayKind::Raw,
                                       .expression = "",
                                       .searchable = true,
                                       .writable = true});

  const QString inputDirPath = QFINDTESTDATA("parser_inputs");
  QVERIFY(!inputDirPath.isEmpty());
  const QString sourcePath =
      QDir(inputDirPath)
          .filePath("高田憂希;寿美菜子 - "
                    "TVアニメ「やがて君になる」エンディングテーマ「hectopascal"
                    "」 - 1 - 2 - 好き、以外の言葉で.flac");
  QVERIFY(QFileInfo::exists(sourcePath));
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QString editedPath = QDir(tempDir.path()).filePath("edited.flac");
  QVERIFY(QFile::copy(sourcePath, editedPath));

  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("OldTitle", "OldArtist", editedPath, "1"));
  const int songPk = pl->getPkByIndex(0);
  QVERIFY(songPk >= 0);

  QTableView *table =
      tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);
  const QModelIndex row0 = table->model()->index(0, 0);
  QVERIFY(row0.isValid());
  const QPoint validPos = table->visualRect(row0).center();

  QTimer::singleShot(0, []() {
    if (QWidget *popup = QApplication::activePopupWidget()) {
      popup->close();
    }
  });
  tabs_->onCustomContextMenuRequested(validPos);
  QVERIFY(tabs_->propertiesAction()->isEnabled());
  tabs_->propertiesAction()->trigger();
  QTest::qWait(0);

  SongPropertiesDialog *dialog = nullptr;
  for (QWidget *widget : QApplication::topLevelWidgets()) {
    dialog = qobject_cast<SongPropertiesDialog *>(widget);
    if (dialog != nullptr) {
      break;
    }
  }
  QVERIFY(dialog != nullptr);
  QSignalSpy updatedSpy(dialog, &SongPropertiesDialog::songUpdated);
  QSignalSpy rowSpy(pl, &Playlist::songDataChanged);

  QTableView *propertiesTable =
      dialog->findChild<QTableView *>("properties_table");
  QVERIFY(propertiesTable != nullptr);
  QAbstractItemModel *model = propertiesTable->model();
  QVERIFY(model != nullptr);

  int lyricsRow = -1;
  for (int row = 0; row < model->rowCount(); ++row) {
    const QString fieldText =
        model->data(model->index(row, 0), Qt::DisplayRole).toString();
    if (fieldText.contains("(attr:lyrics)")) {
      lyricsRow = row;
      break;
    }
  }
  QVERIFY(lyricsRow >= 0);

  const QModelIndex lyricsIndex = model->index(lyricsRow, 1);
  QVERIFY(lyricsIndex.isValid());
  QTimer::singleShot(0, []() {
    QWidget *editWidget = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
      if (widget->metaObject()->className() == QString("FieldEditDialog")) {
        editWidget = widget;
        break;
      }
    }
    QVERIFY(editWidget != nullptr);
    auto *valueEdit = editWidget->findChild<QPlainTextEdit *>("value_edit");
    QVERIFY(valueEdit != nullptr);
    valueEdit->setPlainText("edited-line");
    auto *editButtons = editWidget->findChild<QDialogButtonBox *>("buttonBox");
    QVERIFY(editButtons != nullptr);
    auto *okButton = editButtons->button(QDialogButtonBox::Ok);
    QVERIFY(okButton != nullptr);
    QTest::mouseClick(okButton, Qt::LeftButton);
  });
  QVERIFY(QMetaObject::invokeMethod(dialog, "handleTableDoubleClicked",
                                    Qt::DirectConnection,
                                    Q_ARG(QModelIndex, lyricsIndex)));
  QTest::qWait(0);

  QCOMPARE(model->data(model->index(lyricsRow, 1), Qt::DisplayRole).toString(),
           QString("edited-line"));
  QVERIFY(model->data(model->index(lyricsRow, 0), Qt::DisplayRole)
              .toString()
              .contains('*'));

  auto *buttonBox = dialog->findChild<QDialogButtonBox *>("buttonBox");
  QVERIFY(buttonBox != nullptr);
  auto *saveButton = buttonBox->button(QDialogButtonBox::Save);
  QVERIFY(saveButton != nullptr);
  QTest::mouseClick(saveButton, Qt::LeftButton);
  QTest::qWait(0);

  QCOMPARE(updatedSpy.count(), 1);
  const QList<QVariant> args = updatedSpy.takeFirst();
  QCOMPARE(args.at(0).toInt(), songPk);
  QCOMPARE(rowSpy.count(), 1);

  std::unordered_map<std::string, std::string> remainingAfter;
  const std::optional<MSong> parseResult =
      SongParser::parse(editedPath.toStdString(), *registry_, &remainingAfter);
  QVERIFY(parseResult.has_value());
  const MSong &parsedAfter = *parseResult;
  QVERIFY(parsedAfter.contains("attr:lyrics"));
  QCOMPARE(parsedAfter.at("attr:lyrics").text, std::string("edited-line"));
}

void TestPlaylistTabs::propertiesDialog_cancel_discardsBufferedEdits() {
  registry_->addOrUpdateDynamicColumn({.id = "attr:lyrics",
                                       .title = "Lyrics",
                                       .sortable = true,
                                       .visibleByDefault = true,
                                       .defaultWidth = 180},
                                      {.id = "attr:lyrics",
                                       .source = ColumnSource::SongAttribute,
                                       .valueType = ValueType::Text,
                                       .displayKind = DisplayKind::Raw,
                                       .expression = "",
                                       .searchable = true,
                                       .writable = true});

  const QString inputDirPath = QFINDTESTDATA("parser_inputs");
  QVERIFY(!inputDirPath.isEmpty());
  const QString sourcePath =
      QDir(inputDirPath)
          .filePath("高田憂希;寿美菜子 - "
                    "TVアニメ「やがて君になる」エンディングテーマ「hectopascal"
                    "」 - 1 - 2 - 好き、以外の言葉で.flac");
  QVERIFY(QFileInfo::exists(sourcePath));
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QString editedPath =
      QDir(tempDir.path()).filePath("edited-cancel.flac");
  QVERIFY(QFile::copy(sourcePath, editedPath));

  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("OldTitle", "OldArtist", editedPath, "1"));

  QTableView *table =
      tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);
  const QModelIndex row0 = table->model()->index(0, 0);
  QVERIFY(row0.isValid());
  const QPoint validPos = table->visualRect(row0).center();

  QTimer::singleShot(0, []() {
    if (QWidget *popup = QApplication::activePopupWidget()) {
      popup->close();
    }
  });
  tabs_->onCustomContextMenuRequested(validPos);
  tabs_->propertiesAction()->trigger();
  QTest::qWait(0);

  SongPropertiesDialog *dialog = nullptr;
  for (QWidget *widget : QApplication::topLevelWidgets()) {
    dialog = qobject_cast<SongPropertiesDialog *>(widget);
    if (dialog != nullptr) {
      break;
    }
  }
  QVERIFY(dialog != nullptr);
  QTableView *propertiesTable =
      dialog->findChild<QTableView *>("properties_table");
  QVERIFY(propertiesTable != nullptr);
  QAbstractItemModel *model = propertiesTable->model();
  QVERIFY(model != nullptr);

  int lyricsRow = -1;
  for (int row = 0; row < model->rowCount(); ++row) {
    const QString fieldText =
        model->data(model->index(row, 0), Qt::DisplayRole).toString();
    if (fieldText.contains("(attr:lyrics)")) {
      lyricsRow = row;
      break;
    }
  }
  QVERIFY(lyricsRow >= 0);

  const QModelIndex lyricsIndex = model->index(lyricsRow, 1);
  QTimer::singleShot(0, []() {
    QWidget *editWidget = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
      if (widget->metaObject()->className() == QString("FieldEditDialog")) {
        editWidget = widget;
        break;
      }
    }
    QVERIFY(editWidget != nullptr);
    auto *valueEdit = editWidget->findChild<QPlainTextEdit *>("value_edit");
    QVERIFY(valueEdit != nullptr);
    valueEdit->setPlainText("should-not-save");
    auto *editButtons = editWidget->findChild<QDialogButtonBox *>("buttonBox");
    QVERIFY(editButtons != nullptr);
    auto *okButton = editButtons->button(QDialogButtonBox::Ok);
    QVERIFY(okButton != nullptr);
    QTest::mouseClick(okButton, Qt::LeftButton);
  });
  QVERIFY(QMetaObject::invokeMethod(dialog, "handleTableDoubleClicked",
                                    Qt::DirectConnection,
                                    Q_ARG(QModelIndex, lyricsIndex)));
  QTest::qWait(0);
  QVERIFY(model->data(model->index(lyricsRow, 0), Qt::DisplayRole)
              .toString()
              .contains('*'));

  auto *buttonBox = dialog->findChild<QDialogButtonBox *>("buttonBox");
  QVERIFY(buttonBox != nullptr);
  auto *cancelButton = buttonBox->button(QDialogButtonBox::Cancel);
  QVERIFY(cancelButton != nullptr);
  QTest::mouseClick(cancelButton, Qt::LeftButton);
  QTest::qWait(0);

  std::unordered_map<std::string, std::string> remainingAfter;
  const std::optional<MSong> parseResult =
      SongParser::parse(editedPath.toStdString(), *registry_, &remainingAfter);
  QVERIFY(parseResult.has_value());
  const MSong &parsedAfter = *parseResult;
  if (parsedAfter.contains("attr:lyrics")) {
    QVERIFY(parsedAfter.at("attr:lyrics").text !=
            std::string("should-not-save"));
  }
  if (remainingAfter.contains("lyrics")) {
    QVERIFY(remainingAfter.at("lyrics") != std::string("should-not-save"));
  }
}

void TestPlaylistTabs::propertiesDialog_removeField_removesNonBuiltInOnSave() {
  registry_->addOrUpdateDynamicColumn({.id = "attr:lyrics",
                                       .title = "Lyrics",
                                       .sortable = true,
                                       .visibleByDefault = true,
                                       .defaultWidth = 180},
                                      {.id = "attr:lyrics",
                                       .source = ColumnSource::SongAttribute,
                                       .valueType = ValueType::Text,
                                       .displayKind = DisplayKind::Raw,
                                       .expression = "",
                                       .searchable = true,
                                       .writable = true});

  const QString inputDirPath = QFINDTESTDATA("parser_inputs");
  QVERIFY(!inputDirPath.isEmpty());
  const QString sourcePath =
      QDir(inputDirPath)
          .filePath("高田憂希;寿美菜子 - "
                    "TVアニメ「やがて君になる」エンディングテーマ「hectopascal"
                    "」 - 1 - 2 - 好き、以外の言葉で.flac");
  QVERIFY(QFileInfo::exists(sourcePath));
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QString editedPath =
      QDir(tempDir.path()).filePath("edited-remove-attr.flac");
  QVERIFY(QFile::copy(sourcePath, editedPath));

  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("OldTitle", "OldArtist", editedPath, "1"));

  QTableView *table =
      tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);
  const QModelIndex row0 = table->model()->index(0, 0);
  QVERIFY(row0.isValid());
  const QPoint validPos = table->visualRect(row0).center();

  QTimer::singleShot(0, []() {
    if (QWidget *popup = QApplication::activePopupWidget()) {
      popup->close();
    }
  });
  tabs_->onCustomContextMenuRequested(validPos);
  tabs_->propertiesAction()->trigger();
  QTest::qWait(0);

  SongPropertiesDialog *dialog = nullptr;
  for (QWidget *widget : QApplication::topLevelWidgets()) {
    dialog = qobject_cast<SongPropertiesDialog *>(widget);
    if (dialog != nullptr) {
      break;
    }
  }
  QVERIFY(dialog != nullptr);
  QTableView *propertiesTable =
      dialog->findChild<QTableView *>("properties_table");
  QVERIFY(propertiesTable != nullptr);
  QAbstractItemModel *model = propertiesTable->model();
  QVERIFY(model != nullptr);

  int lyricsRow = -1;
  for (int row = 0; row < model->rowCount(); ++row) {
    const QString fieldText =
        model->data(model->index(row, 0), Qt::DisplayRole).toString();
    if (fieldText.contains("(attr:lyrics)")) {
      lyricsRow = row;
      break;
    }
  }
  QVERIFY(lyricsRow >= 0);
  const int beforeRowCount = model->rowCount();

  propertiesTable->selectRow(lyricsRow);
  auto *removeButton = dialog->findChild<QPushButton *>("remove_field_button");
  QVERIFY(removeButton != nullptr);
  QVERIFY(removeButton->isEnabled());
  QTest::mouseClick(removeButton, Qt::LeftButton);
  QTest::qWait(0);
  QCOMPARE(model->rowCount(), beforeRowCount - 1);

  auto *buttonBox = dialog->findChild<QDialogButtonBox *>("buttonBox");
  QVERIFY(buttonBox != nullptr);
  auto *saveButton = buttonBox->button(QDialogButtonBox::Save);
  QVERIFY(saveButton != nullptr);
  QTest::mouseClick(saveButton, Qt::LeftButton);
  QTest::qWait(0);

  std::unordered_map<std::string, std::string> remainingAfter;
  const std::optional<MSong> parseResult =
      SongParser::parse(editedPath.toStdString(), *registry_, &remainingAfter);
  QVERIFY(parseResult.has_value());
  const MSong &parsedAfter = *parseResult;
  QVERIFY(!parsedAfter.contains("attr:lyrics"));
  QVERIFY(!remainingAfter.contains("lyrics"));
}

void TestPlaylistTabs::propertiesDialog_removeField_blocksBuiltIn() {
  registry_->addOrUpdateDynamicColumn({.id = "attr:lyrics",
                                       .title = "Lyrics",
                                       .sortable = true,
                                       .visibleByDefault = true,
                                       .defaultWidth = 180},
                                      {.id = "attr:lyrics",
                                       .source = ColumnSource::SongAttribute,
                                       .valueType = ValueType::Text,
                                       .displayKind = DisplayKind::Raw,
                                       .expression = "",
                                       .searchable = true,
                                       .writable = true});

  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(
      makeSong("OldTitle", "OldArtist", "/tmp/remove-built-in.mp3", "1"));

  QTableView *table =
      tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);
  const QModelIndex row0 = table->model()->index(0, 0);
  QVERIFY(row0.isValid());
  const QPoint validPos = table->visualRect(row0).center();

  QTimer::singleShot(0, []() {
    if (QWidget *popup = QApplication::activePopupWidget()) {
      popup->close();
    }
  });
  tabs_->onCustomContextMenuRequested(validPos);
  tabs_->propertiesAction()->trigger();
  QTest::qWait(0);

  SongPropertiesDialog *dialog = nullptr;
  for (QWidget *widget : QApplication::topLevelWidgets()) {
    dialog = qobject_cast<SongPropertiesDialog *>(widget);
    if (dialog != nullptr) {
      break;
    }
  }
  QVERIFY(dialog != nullptr);
  QTableView *propertiesTable =
      dialog->findChild<QTableView *>("properties_table");
  QVERIFY(propertiesTable != nullptr);
  QAbstractItemModel *model = propertiesTable->model();
  QVERIFY(model != nullptr);

  int titleRow = -1;
  for (int row = 0; row < model->rowCount(); ++row) {
    const QString fieldText =
        model->data(model->index(row, 0), Qt::DisplayRole).toString();
    if (fieldText.contains("(title)")) {
      titleRow = row;
      break;
    }
  }
  QVERIFY(titleRow >= 0);
  const int beforeRowCount = model->rowCount();

  propertiesTable->selectRow(titleRow);
  auto *removeButton = dialog->findChild<QPushButton *>("remove_field_button");
  QVERIFY(removeButton != nullptr);
  QTest::mouseClick(removeButton, Qt::LeftButton);
  QTest::qWait(0);
  QCOMPARE(model->rowCount(), beforeRowCount);

  dialog->close();
  delete dialog;
}

void TestPlaylistTabs::propertiesDialog_addField_addsAndSavesTag() {
  const QString inputDirPath = QFINDTESTDATA("parser_inputs");
  QVERIFY(!inputDirPath.isEmpty());
  const QString sourcePath =
      QDir(inputDirPath)
          .filePath("高田憂希;寿美菜子 - "
                    "TVアニメ「やがて君になる」エンディングテーマ「hectopascal"
                    "」 - 1 - 2 - 好き、以外の言葉で.flac");
  QVERIFY(QFileInfo::exists(sourcePath));
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QString editedPath = QDir(tempDir.path()).filePath("edited-add.flac");
  QVERIFY(QFile::copy(sourcePath, editedPath));

  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("OldTitle", "OldArtist", editedPath, "1"));

  QTableView *table =
      tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);
  const QModelIndex row0 = table->model()->index(0, 0);
  QVERIFY(row0.isValid());
  const QPoint validPos = table->visualRect(row0).center();

  QTimer::singleShot(0, []() {
    if (QWidget *popup = QApplication::activePopupWidget()) {
      popup->close();
    }
  });
  tabs_->onCustomContextMenuRequested(validPos);
  tabs_->propertiesAction()->trigger();
  QTest::qWait(0);

  SongPropertiesDialog *dialog = nullptr;
  for (QWidget *widget : QApplication::topLevelWidgets()) {
    dialog = qobject_cast<SongPropertiesDialog *>(widget);
    if (dialog != nullptr) {
      break;
    }
  }
  QVERIFY(dialog != nullptr);

  auto *addButton = dialog->findChild<QPushButton *>("add_field_button");
  QVERIFY(addButton != nullptr);
  auto *buttonBox = dialog->findChild<QDialogButtonBox *>("buttonBox");
  QVERIFY(buttonBox != nullptr);
  auto *saveButton = buttonBox->button(QDialogButtonBox::Save);
  QVERIFY(saveButton != nullptr);

  QTimer::singleShot(0, []() {
    QWidget *addWidget = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
      if (widget->metaObject()->className() == QString("AddFieldDialog")) {
        addWidget = widget;
        break;
      }
    }
    QVERIFY(addWidget != nullptr);
    auto *nameEdit = addWidget->findChild<QLineEdit *>("name_edit");
    QVERIFY(nameEdit != nullptr);
    nameEdit->setText("mood");
    auto *valueEdit = addWidget->findChild<QPlainTextEdit *>("value_edit");
    QVERIFY(valueEdit != nullptr);
    valueEdit->setPlainText("calm");
    auto *editButtons = addWidget->findChild<QDialogButtonBox *>("buttonBox");
    QVERIFY(editButtons != nullptr);
    auto *okButton = editButtons->button(QDialogButtonBox::Ok);
    QVERIFY(okButton != nullptr);
    QTest::mouseClick(okButton, Qt::LeftButton);
  });
  QTest::mouseClick(addButton, Qt::LeftButton);
  QTest::qWait(0);

  QTest::mouseClick(saveButton, Qt::LeftButton);
  QTest::qWait(0);

  std::unordered_map<std::string, std::string> remainingAfter;
  const std::optional<MSong> parseResult =
      SongParser::parse(editedPath.toStdString(), *registry_, &remainingAfter);
  QVERIFY(parseResult.has_value());
  const MSong &parsedAfter = *parseResult;
  QVERIFY(parsedAfter.contains("attr:mood") || remainingAfter.contains("mood"));
  if (parsedAfter.contains("attr:mood")) {
    QCOMPARE(parsedAfter.at("attr:mood").text, std::string("calm"));
  } else {
    QCOMPARE(remainingAfter.at("mood"), std::string("calm"));
  }
}

void TestPlaylistTabs::
    createNewPlaylistTabFromSongIds_createsTabAndCopiesSongs() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-search-a.mp3", "1"));
  pl->addSong(makeSong("B", "Artist", "/tmp/pt-search-b.mp3", "2"));

  const QList<int> songIds = {pl->getPkByIndex(0), pl->getPkByIndex(1)};
  tabs_->createNewPlaylistTabFromSongIds(songIds);

  QCOMPARE(tabs_->tabWidget()->count(), 2);
  QCOMPARE(tabs_->tabWidget()->currentIndex(), 1);
  Playlist *newPlaylist = tabs_->currentPlaylist();
  QVERIFY(newPlaylist != nullptr);
  QCOMPARE(newPlaylist->songCount(), 2);
  QCOMPARE(newPlaylist->getSongByIndex(0).at("title").text, std::string("A"));
  QCOMPARE(newPlaylist->getSongByIndex(1).at("title").text, std::string("B"));
}

void TestPlaylistTabs::createNewPlaylistTabFromSongIds_emptyNoop() {
  QCOMPARE(tabs_->tabWidget()->count(), 1);
  tabs_->createNewPlaylistTabFromSongIds({});
  QCOMPARE(tabs_->tabWidget()->count(), 1);
}

void TestPlaylistTabs::startupRestore_loadsAllPlaylistsInSavedOrder() {
  const int playlistA = insertPlaylistRow(databaseManager_->db(), "A", 1);
  const int playlistB = insertPlaylistRow(databaseManager_->db(), "B", 2);
  QVERIFY(playlistA > 0);
  QVERIFY(playlistB > 0);
  QVERIFY(updatePlaylistTabOrder(databaseManager_->db(),
                                 {1, playlistB, playlistA}));

  recreateTabs();

  QCOMPARE(tabs_->tabWidget()->count(), 3);
  QCOMPARE(tabs_->tabWidget()->tabText(0), QString("Default"));
  QCOMPARE(tabs_->tabWidget()->tabText(1), QString("B"));
  QCOMPARE(tabs_->tabWidget()->tabText(2), QString("A"));
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(0).toInt(), 1);
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(1).toInt(), playlistB);
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(2).toInt(), playlistA);
}

void TestPlaylistTabs::startupRestore_restoresLastOpenedPlaylistTab() {
  const int playlistA = insertPlaylistRow(databaseManager_->db(), "A", 1);
  const int playlistB = insertPlaylistRow(databaseManager_->db(), "B", 2);
  QVERIFY(playlistA > 0);
  QVERIFY(playlistB > 0);
  QVERIFY(updatePlaylistTabOrder(databaseManager_->db(),
                                 {1, playlistA, playlistB}));

  QSettings settings;
  settings.setValue("playlist/last_opened_id", playlistB);

  recreateTabs();

  QCOMPARE(tabs_->tabWidget()->currentIndex(), 2);
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(2).toInt(), playlistB);
}

void TestPlaylistTabs::startupRestore_recoversCorruptedTabOrder() {
  const int playlistA = insertPlaylistRow(databaseManager_->db(), "A", 1);
  const int playlistB = insertPlaylistRow(databaseManager_->db(), "B", 2);
  QVERIFY(playlistA > 0);
  QVERIFY(playlistB > 0);

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("UPDATE playlists SET tab_order=1 WHERE playlist_id=1"));
  QVERIFY(
      q.exec(QString("UPDATE playlists SET tab_order=2 WHERE playlist_id=%1")
                 .arg(playlistA)));
  QVERIFY(
      q.exec(QString("UPDATE playlists SET tab_order=3 WHERE playlist_id=%1")
                 .arg(playlistB)));

  recreateTabs();

  QCOMPARE(tabs_->tabWidget()->count(), 3);
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(0).toInt(), 1);
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(1).toInt(), playlistA);
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(2).toInt(), playlistB);

  QVERIFY(q.exec("SELECT tab_order FROM playlists ORDER BY tab_order ASC"));
  int expected = 0;
  while (q.next()) {
    QCOMPARE(q.value(0).toInt(), expected);
    expected += 1;
  }
  QCOMPARE(expected, 3);
}

void TestPlaylistTabs::removeTab_deletesNonDefaultPlaylist() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-remove-a.mp3", "1"));

  const QList<int> songIds = {pl->getPkByIndex(0)};
  tabs_->createNewPlaylistTabFromSongIds(songIds);
  QCOMPARE(tabs_->tabWidget()->count(), 2);

  const int removedPlaylistId =
      tabs_->tabWidget()->tabBar()->tabData(1).toInt();
  QVERIFY(removedPlaylistId > 1);

  QSqlQuery q(databaseManager_->db());
  QVERIFY(
      q.exec(QString("SELECT COUNT(*) FROM playlist_items WHERE playlist_id=%1")
                 .arg(removedPlaylistId)));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);

  QCOMPARE(tabs_->tabWidget()->currentIndex(), 1);
  QVERIFY(tabs_->removePlaylistTabByIndex(1));
  QCOMPARE(tabs_->tabWidget()->count(), 1);
  QVERIFY(collection_->playlist(removedPlaylistId) == nullptr);
  QCOMPARE(tabs_->currentPlaylist(), collection_->playlist(1));

  QVERIFY(q.exec(QString("SELECT COUNT(*) FROM playlists WHERE playlist_id=%1")
                     .arg(removedPlaylistId)));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);

  QVERIFY(
      q.exec(QString("SELECT COUNT(*) FROM playlist_items WHERE playlist_id=%1")
                 .arg(removedPlaylistId)));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

void TestPlaylistTabs::removeTab_rejectsWhenSingleTab() {
  QCOMPARE(tabs_->tabWidget()->count(), 1);
  QVERIFY(!tabs_->removePlaylistTabByIndex(0));
  QCOMPARE(tabs_->tabWidget()->count(), 1);
}

void TestPlaylistTabs::removeTab_invalidIndexRejected() {
  QCOMPARE(tabs_->tabWidget()->count(), 1);
  QVERIFY(!tabs_->removePlaylistTabByIndex(-1));
  QVERIFY(!tabs_->removePlaylistTabByIndex(99));
  QCOMPARE(tabs_->tabWidget()->count(), 1);
}

void TestPlaylistTabs::removeTab_allowsRemovingDefaultWhenMultipleTabs() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-remove-default-a.mp3", "1"));

  tabs_->createNewPlaylistTabFromSongIds({pl->getPkByIndex(0)});
  QCOMPARE(tabs_->tabWidget()->count(), 2);
  QCOMPARE(tabs_->tabWidget()->tabBar()->tabData(0).toInt(), 1);

  QVERIFY(tabs_->removePlaylistTabByIndex(0));
  QCOMPARE(tabs_->tabWidget()->count(), 1);
  QVERIFY(tabs_->tabWidget()->tabBar()->tabData(0).toInt() != 1);

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("SELECT COUNT(*) FROM playlists WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

void TestPlaylistTabs::tabMove_persistsOrderInDb() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl != nullptr);
  pl->addSong(makeSong("A", "Artist", "/tmp/pt-move-a.mp3", "1"));
  pl->addSong(makeSong("B", "Artist", "/tmp/pt-move-b.mp3", "2"));

  tabs_->createNewPlaylistTabFromSongIds({pl->getPkByIndex(0)});
  tabs_->createNewPlaylistTabFromSongIds({pl->getPkByIndex(1)});
  QCOMPARE(tabs_->tabWidget()->count(), 3);

  const int idAt1 = tabs_->tabWidget()->tabBar()->tabData(1).toInt();
  const int idAt2 = tabs_->tabWidget()->tabBar()->tabData(2).toInt();
  QVERIFY(idAt1 > 1);
  QVERIFY(idAt2 > 1);

  tabs_->tabWidget()->tabBar()->moveTab(2, 1);

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec(QString("SELECT tab_order FROM playlists WHERE playlist_id=%1")
                     .arg(idAt1)));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 2);
  QVERIFY(q.exec(QString("SELECT tab_order FROM playlists WHERE playlist_id=%1")
                     .arg(idAt2)));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);
  QVERIFY(q.exec("SELECT tab_order FROM playlists WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

void TestPlaylistTabs::renameTab_updatesTabTextAndDb() {
  const int playlistId = insertPlaylistRow(databaseManager_->db(), "Old", 1);
  QVERIFY(playlistId > 0);

  recreateTabs();

  QCOMPARE(tabs_->tabWidget()->count(), 2);
  QCOMPARE(tabs_->tabWidget()->tabText(1), QString("Old"));

  QVERIFY(tabs_->renamePlaylistTabByIndex(1, "Renamed"));
  QCOMPARE(tabs_->tabWidget()->tabText(1), QString("Renamed"));

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec(QString("SELECT name FROM playlists WHERE playlist_id=%1")
                     .arg(playlistId)));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("Renamed"));
}

namespace {
// The last (playlist, songPk) the spy recorded.
std::pair<Playlist *, int> lastSelected(const QSignalSpy &spy) {
  const QList<QVariant> &args = spy.last();
  return {args.at(0).value<Playlist *>(), args.at(1).toInt()};
}

void selectRows(QTableView *table, const QList<int> &rows) {
  QItemSelection selection;
  for (int row : rows) {
    const QModelIndex index = table->model()->index(row, 0);
    selection.select(index, index);
  }
  table->selectionModel()->select(selection,
                                  QItemSelectionModel::ClearAndSelect |
                                      QItemSelectionModel::Rows);
}
} // namespace

void TestPlaylistTabs::selectedSongChanged_followsSelectionAndTabs() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl->addSong(makeSong("A", "Artist", "/tmp/pt-sel-a.mp3")));
  QVERIFY(pl->addSong(makeSong("B", "Artist", "/tmp/pt-sel-b.mp3")));
  auto *table = tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  QSignalSpy spy(tabs_, &PlaylistTabs::selectedSongChanged);

  selectRows(table, {1});
  QCOMPARE(spy.count(), 1);
  QCOMPARE(lastSelected(spy), std::make_pair(pl, pl->getPkByIndex(1)));

  // The smallest selected row wins.
  selectRows(table, {1, 0});
  QCOMPARE(lastSelected(spy), std::make_pair(pl, pl->getPkByIndex(0)));

  table->clearSelection();
  QCOMPARE(lastSelected(spy), std::make_pair(pl, -1));

  // Switching tabs reports the new tab's selection, and back.
  selectRows(table, {1});
  const int other = collection_->createPlaylist("Other");
  tabs_->tabWidget()->setCurrentIndex(1);
  QCOMPARE(lastSelected(spy), std::make_pair(collection_->playlist(other), -1));
  tabs_->tabWidget()->setCurrentIndex(0);
  QCOMPARE(lastSelected(spy), std::make_pair(pl, pl->getPkByIndex(1)));
}

void TestPlaylistTabs::selectedSongChanged_followsRemovalAndSort() {
  Playlist *pl = tabs_->currentPlaylist();
  QVERIFY(pl->addSong(makeSong("A", "Artist", "/tmp/pt-sel-a.mp3")));
  QVERIFY(pl->addSong(makeSong("B", "Artist", "/tmp/pt-sel-b.mp3")));
  QVERIFY(pl->addSong(makeSong("C", "Artist", "/tmp/pt-sel-c.mp3")));
  const int a = pl->getPkByIndex(0);
  const int c = pl->getPkByIndex(2);
  auto *table = tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  selectRows(table, {0, 2});
  QSignalSpy spy(tabs_, &PlaylistTabs::selectedSongChanged);

  // A sort that keeps A first sends nothing.
  pl->sortByField("title", 0);
  QCOMPARE(spy.count(), 0);
  // Descending, C comes first; the selection moves with its songs, without
  // selectionChanged.
  pl->sortByField("title", 1);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(lastSelected(spy), std::make_pair(pl, c));

  // Removing an unselected row renumbers the selection but keeps its songs.
  pl->removeSongs({1});
  QCOMPARE(spy.count(), 1);
  // Removing the first selected row makes the next one first.
  pl->removeSongs({pl->getIndexByPk(c)});
  QCOMPARE(lastSelected(spy), std::make_pair(pl, a));
  pl->removeSongs({pl->getIndexByPk(a)});
  QCOMPARE(lastSelected(spy), std::make_pair(pl, -1));
}

void TestPlaylistTabs::selectedSongChanged_followsAutoPlaylistReset() {
  watchSongs({"a.mp3", "b.mp3"});
  const int id = collection_->createAutoPlaylist("Auto", "true");
  tabs_->tabWidget()->setCurrentIndex(1);
  Playlist *autoPlaylist = collection_->playlist(id);
  QCOMPARE(tabs_->currentPlaylist(), autoPlaylist);
  auto *table = tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  selectRows(table, {0});
  QSignalSpy spy(tabs_, &PlaylistTabs::selectedSongChanged);

  // A reset clears the selection without selectionChanged.
  QVERIFY(collection_->setAutoPlaylistExpression(id, "false"));
  QCOMPARE(spy.count(), 1);
  QCOMPARE(lastSelected(spy), std::make_pair(autoPlaylist, -1));
}

void TestPlaylistTabs::watchSongs(const QStringList &fileNames) {
  songDir_ = new QTemporaryDir();
  QVERIFY(songDir_->isValid());
  const QString root = QFileInfo(songDir_->path()).canonicalFilePath();
  for (const QString &name : fileNames) {
    QFile file(root + "/" + name);
    QVERIFY(file.open(QIODevice::WriteOnly));
  }
  QVERIFY(monitored_->addFolder(root).folderId > 0);
  QTRY_VERIFY(!monitored_->isScanning());
  QCOMPARE(monitored_->songPks().size(), static_cast<size_t>(fileNames.size()));
}

void TestPlaylistTabs::closeNextPopup(QStringList *actionTexts) {
  QTimer::singleShot(0, [actionTexts]() {
    if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
      for (QAction *action : menu->actions()) {
        if (action->isVisible() && !action->isSeparator()) {
          actionTexts->push_back(action->text());
        }
      }
      menu->close();
    }
  });
}

void TestPlaylistTabs::autoPlaylistTab_showsIconAndErrorTooltip() {
  const int id = collection_->createAutoPlaylist("Auto", "true");
  QTabWidget *tabWidget = tabs_->tabWidget();
  QCOMPARE(tabWidget->count(), 2);
  QVERIFY(tabWidget->tabIcon(0).isNull());
  QVERIFY(!tabWidget->tabIcon(1).isNull());
  QCOMPARE(tabWidget->tabToolTip(1), QString("true"));
  const qint64 validIcon = tabWidget->tabIcon(1).cacheKey();

  QVERIFY(collection_->setAutoPlaylistExpression(id, "davis"));
  QCOMPARE(tabWidget->tabToolTip(1), QString("Expression must be a condition"));
  QVERIFY(tabWidget->tabIcon(1).cacheKey() != validIcon);

  // Loaded autoplaylists get the same icon.
  recreateTabs();
  QCOMPARE(tabs_->tabWidget()->tabToolTip(1),
           QString("Expression must be a condition"));
}

void TestPlaylistTabs::autoPlaylistMenus_leaveOutWritesAndOfferEdit() {
  if (QGuiApplication::primaryScreen() == nullptr) {
    QSKIP("No screen available for popup menu test");
  }
  watchSongs({"a.mp3"});
  QSignalSpy changedSpy(tabs_, &PlaylistTabs::currentPlaylistChanged);
  collection_->createAutoPlaylist("Auto", "true");
  QCOMPARE(changedSpy.count(), 1);
  Playlist *autoPlaylist = tabs_->currentPlaylist();
  QCOMPARE(changedSpy.at(0).at(0).value<Playlist *>(), autoPlaylist);
  QCOMPARE(autoPlaylist->kind(), Playlist::Kind::Auto);
  QCOMPARE(autoPlaylist->songCount(), 1);

  QTableView *table =
      tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);
  const QPoint rowPos = table->visualRect(table->model()->index(0, 0)).center();
  QStringList rowActions;
  closeNextPopup(&rowActions);
  tabs_->onCustomContextMenuRequested(rowPos);
  QVERIFY(rowActions.contains("Play Next"));
  QVERIFY(rowActions.contains("Properties"));
  QVERIFY(!rowActions.contains("Remove from Playlist"));
  QVERIFY(!rowActions.contains("Clear Playlist"));

  QTabBar *tabBar = tabs_->tabWidget()->tabBar();
  QStringList autoTabActions;
  closeNextPopup(&autoTabActions);
  tabs_->onTabContextMenuRequested(tabBar->tabRect(1).center());
  QVERIFY(autoTabActions.contains("Edit Autoplaylist..."));
  QVERIFY(autoTabActions.contains("Remove Tab"));

  // The manual playlist keeps its write operations and has nothing to edit.
  tabs_->tabWidget()->setCurrentIndex(0);
  QCOMPARE(changedSpy.count(), 2);
  tabs_->currentPlaylist()->addSong(
      makeSong("M", "Artist", "/tmp/pt-auto-menu-m.mp3"));
  table = tabs_->tabWidget()->currentWidget()->findChild<QTableView *>();
  rowActions.clear();
  closeNextPopup(&rowActions);
  tabs_->onCustomContextMenuRequested(
      table->visualRect(table->model()->index(0, 0)).center());
  QVERIFY(rowActions.contains("Remove from Playlist"));
  QVERIFY(rowActions.contains("Clear Playlist"));
  QStringList manualTabActions;
  closeNextPopup(&manualTabActions);
  tabs_->onTabContextMenuRequested(tabBar->tabRect(0).center());
  QVERIFY(!manualTabActions.contains("Edit Autoplaylist..."));
}

void TestPlaylistTabs::newAutoPlaylist_dialogChecksExpressionAndCreates() {
  watchSongs({"a.mp3", "b.mp3"});
  tabs_->newAutoPlaylist();
  auto *dialog = tabs_->findChild<AutoPlaylistDialog *>();
  QVERIFY(dialog != nullptr);
  auto *nameEdit = dialog->findChild<QLineEdit *>("name_edit");
  auto *expressionEdit = dialog->findChild<QLineEdit *>("expression_edit");
  auto *statusLabel = dialog->findChild<QLabel *>("status_label");
  auto *buttons = dialog->findChild<QDialogButtonBox *>();
  QVERIFY(nameEdit != nullptr);
  QVERIFY(expressionEdit != nullptr);
  QVERIFY(statusLabel != nullptr);
  QVERIFY(buttons != nullptr);
  QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
  QCOMPARE(nameEdit->text(), QString("New Autoplaylist"));
  QVERIFY(!ok->isEnabled());

  expressionEdit->setText("title IS a");
  QCOMPARE(statusLabel->text(), QString("1 matching song"));
  QVERIFY(ok->isEnabled());
  expressionEdit->setText("true");
  QCOMPARE(statusLabel->text(), QString("2 matching songs"));
  expressionEdit->setText("davis");
  QCOMPARE(statusLabel->text(), QString("Expression must be a condition"));
  QVERIFY(!ok->isEnabled());
  expressionEdit->setText("title IS");
  QVERIFY(statusLabel->text().contains("(at "));
  QVERIFY(!ok->isEnabled());

  expressionEdit->setText("title IS b");
  nameEdit->setText("  ");
  QVERIFY(!ok->isEnabled());
  nameEdit->setText(" B songs ");
  QVERIFY(ok->isEnabled());
  QTest::mouseClick(ok, Qt::LeftButton);

  QCOMPARE(tabs_->tabWidget()->count(), 2);
  QCOMPARE(tabs_->tabWidget()->tabText(1), QString("B songs"));
  const int id = tabs_->tabWidget()->tabBar()->tabData(1).toInt();
  QVERIFY(collection_->isAuto(id));
  QCOMPARE(collection_->autoPlaylistExpression(id), QString("title IS b"));
  QCOMPARE(tabs_->currentPlaylist()->songCount(), 1);
  QTRY_VERIFY(tabs_->findChild<AutoPlaylistDialog *>() == nullptr);

  // A second one gets a numbered default name; Cancel creates nothing.
  collection_->renamePlaylist(id, "New Autoplaylist");
  tabs_->newAutoPlaylist("true");
  dialog = tabs_->findChild<AutoPlaylistDialog *>();
  QVERIFY(dialog != nullptr);
  QCOMPARE(dialog->name(), QString("New Autoplaylist 1"));
  QCOMPARE(dialog->expression(), QString("true"));
  dialog->reject();
  QCOMPARE(tabs_->tabWidget()->count(), 2);
}

void TestPlaylistTabs::editAutoPlaylist_renamesAndSetsExpression() {
  watchSongs({"a.mp3", "b.mp3"});
  const int id = collection_->createAutoPlaylist("Auto", "title IS a");
  tabs_->editAutoPlaylist(id);
  auto *dialog = tabs_->findChild<AutoPlaylistDialog *>();
  QVERIFY(dialog != nullptr);
  QCOMPARE(dialog->name(), QString("Auto"));
  QCOMPARE(dialog->expression(), QString("title IS a"));
  dialog->findChild<QLineEdit *>("name_edit")->setText("Everything");
  dialog->findChild<QLineEdit *>("expression_edit")->setText("true");
  dialog->accept();

  QCOMPARE(tabs_->tabWidget()->tabText(1), QString("Everything"));
  QCOMPARE(collection_->autoPlaylistExpression(id), QString("true"));
  QCOMPARE(tabs_->tabWidget()->tabToolTip(1), QString("true"));
  QCOMPARE(collection_->playlist(id)->songCount(), 2);
  QTRY_VERIFY(tabs_->findChild<AutoPlaylistDialog *>() == nullptr);

  // Renaming alone keeps the rows (and their order).
  QSignalSpy resetSpy(collection_->playlist(id), &Playlist::songsReset);
  tabs_->editAutoPlaylist(id);
  dialog = tabs_->findChild<AutoPlaylistDialog *>();
  QVERIFY(dialog != nullptr);
  dialog->findChild<QLineEdit *>("name_edit")->setText("All");
  dialog->accept();
  QCOMPARE(tabs_->tabWidget()->tabText(1), QString("All"));
  QCOMPARE(resetSpy.count(), 0);

  // A manual playlist has no autoplaylist to edit.
  tabs_->editAutoPlaylist(1);
  QTRY_VERIFY(tabs_->findChild<AutoPlaylistDialog *>() == nullptr);
}

QTEST_MAIN(TestPlaylistTabs)
#include "tst_playlisttabs.moc"
