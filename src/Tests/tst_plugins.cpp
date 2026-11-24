#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontComboBox>
#include <QLibrary>
#include <QObject>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>
#include <QUuid>
#include <QWidget>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../dummyaudioplayer.h"
#include "../monitoredlibrary.h"
#include "../playbackcontroller.h"
#include "../playlistcollection.h"
#include "../pluginapi/ipanelprovider.h"
#include "../pluginapi/iplayercontrol.h"
#include "../pluginapi/iplayerlistener.h"
#include "../pluginapi/isettingspageprovider.h"
#include "../pluginmanager.h"
#include "../pluginplayerhost.h"
#include "../songlibrary.h"
#include "testaudiofiles.h"

namespace {
MSong makeSong(const QString &title, const std::string &filepath) {
  MSong song;
  song.insert_or_assign("title", FieldValue(title.toStdString(), "title"));
  song.insert_or_assign("artist", FieldValue("Artist", "artist"));
  song.insert_or_assign("album", FieldValue("Album", "album"));
  song.insert_or_assign("filepath", FieldValue(filepath, "filepath"));
  return song;
}

// Records the events it gets, as text.
class RecordingListener : public IPlayerListener {
public:
  void trackStarted(const TrackInfo &track) override {
    events << "started " + track.fields.value("title") + " " +
                  track.tagFields.value("lyrics");
  }
  void stopped() override { events << "stopped"; }
  void paused() override { events << "paused"; }
  void resumed() override { events << "resumed"; }
  void positionChanged(qint64 positionMs) override {
    events << "position " + QString::number(positionMs);
  }
  void durationChanged(qint64 durationMs) override {
    events << "duration " + QString::number(durationMs);
  }
  void seeked(qint64 positionMs) override {
    events << "seeked " + QString::number(positionMs);
  }
  void finished() override { events << "finished"; }

  QStringList events;
};

// Seeks once, inside the delivery of a position.
class SeekingListener : public IPlayerListener {
public:
  SeekingListener(IPlayerControl &control, qint64 atMs, qint64 toMs)
      : control_(control), atMs_(atMs), toMs_(toMs) {}
  void positionChanged(qint64 positionMs) override {
    if (positionMs == atMs_) {
      control_.seek(toMs_);
    }
  }

private:
  IPlayerControl &control_;
  qint64 atMs_;
  qint64 toMs_;
};

// The manager's entry for the plugin with this id; null if none.
const PluginEntry *entryWithId(const PluginManager &manager,
                               const QString &id) {
  for (const PluginEntry &entry : manager.plugins()) {
    if (entry.id == id) {
      return &entry;
    }
  }
  return nullptr;
}
} // namespace

class TestPlugins : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void playerHost_forwardsStartPositionAndStop();
  void playerHost_forwardsPauseResumeDurationSeekAndFinish();
  void playerHost_seekRunsAfterTheDelivery();
  void playerHost_dropsQueuedSeekWhenDestroyed();
  void playerHost_playResumesAfterTheCall();
  void manager_loadsLyricsPlugin();
  void lyricsSettings_loadSaveAndRestyleOpenPanels();
  void manager_skipsNonLibrariesAndDuplicateIds();
  void manager_listsDisabledPluginWithoutLoadingIt();
  void manager_registersSongFieldsAndRefusesClashes();
  void playStats_valuesChangedIsDeferredAndDataStaysInTheDataRoot();

private:
  DummyAudioPlayer *player() const;
  // Adds a song to the playlist and makes it the one Play starts.
  void addSong();
  // Plugins from Tests/plugins, keeping their data in dir_.
  QString pluginFolder() const;
  QString dataRoot() const;

  QTemporaryDir *dir_ = nullptr;
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  MonitoredLibrary *monitored_ = nullptr;
  PlaylistCollection *playlists_ = nullptr;
  PlaybackController *controller_ = nullptr;
  Playlist *playlist_ = nullptr;
  QString connectionName_;
};

void TestPlugins::init() {
  qputenv("MYPLAYER_USE_DUMMY_AUDIO_PLAYER", "1");
  // The play stats plugin's cloud sync reaches no network.
  qputenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC", "1");
  // No plugin disabled, also when a test that disables one failed.
  saveDisabledPluginIds({});
  dir_ = new QTemporaryDir();
  QVERIFY(dir_->isValid());
  connectionName_ =
      QStringLiteral("test_plugins_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  // The refresh on play reads this tag, which has no column.
  library_ = new SongLibrary(
      *registry_, *databaseManager_,
      [](const std::string &path, const ColumnRegistry &,
         std::unordered_map<std::string, std::string> *remainingFields) {
        if (remainingFields) {
          (*remainingFields)["lyrics"] = "[00:01.00]tag";
        }
        return makeSong("Song", path);
      });
  monitored_ = new MonitoredLibrary(*library_, *registry_, *databaseManager_);
  playlists_ =
      new PlaylistCollection(*library_, *databaseManager_, *monitored_);
  playlists_->loadAll();
  playlist_ = playlists_->playlist(1);
  QVERIFY(playlist_ != nullptr);

  controller_ = new PlaybackController(*library_, *playlists_);
  controller_->setBackend(PlaybackBackendManager::Backend::QMediaPlayer);
  controller_->manager().setView(*playlist_);
  controller_->setShownPlaylist([this]() { return playlist_; });
  controller_->setReady(true);
}

void TestPlugins::cleanup() {
  delete controller_;
  controller_ = nullptr;
  delete playlists_;
  playlists_ = nullptr;
  delete monitored_;
  monitored_ = nullptr;
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
  delete dir_;
  dir_ = nullptr;
  qunsetenv("MYPLAYER_USE_DUMMY_AUDIO_PLAYER");
  qunsetenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC");
}

DummyAudioPlayer *TestPlugins::player() const {
  return qobject_cast<DummyAudioPlayer *>(
      controller_->findChild<PlaybackBackendManager *>()->player());
}

void TestPlugins::addSong() {
  const QString path = dir_->filePath("song.wav");
  QVERIFY(writeSilentWav(path));
  playlist_->addSong(makeSong("Song", path.toStdString()));
  playlist_->setLastPlayed(playlist_->getPkByIndex(0));
}

QString TestPlugins::pluginFolder() const {
  return QCoreApplication::applicationDirPath() + "/plugins";
}

QString TestPlugins::dataRoot() const { return dir_->filePath("plugin-data"); }

void TestPlugins::playerHost_forwardsPauseResumeDurationSeekAndFinish() {
  addSong();
  PluginPlayerHost host(*controller_);
  RecordingListener listener;
  host.addListener(&listener);
  controller_->play();
  listener.events.clear();

  controller_->pause();
  controller_->play();
  QCOMPARE(listener.events, (QStringList{"paused", "resumed"}));
  listener.events.clear();

  // The seek is reported before the backend is told (which reports the
  // position).
  controller_->seek(4000);
  QCOMPARE(listener.events.value(0), QString("seeked 4000"));
  listener.events.clear();

  DummyAudioPlayer *audio = player();
  emit audio->durationChanged(90000);
  QCOMPARE(listener.events, QStringList{"duration 90000"});
  listener.events.clear();

  // The end, before playback moves on.
  emit audio->mediaStatusChanged(QMediaPlayer::EndOfMedia);
  QCOMPARE(listener.events.value(0), QString("finished"));
  host.removeListener(&listener);
}

void TestPlugins::playerHost_forwardsStartPositionAndStop() {
  addSong();
  PluginPlayerHost host(*controller_);
  RecordingListener listener;
  host.addListener(&listener);

  // The song with its tag fields from the refresh on play.
  controller_->play();
  QVERIFY(listener.events.contains("started Song [00:01.00]tag"));
  listener.events.clear();

  player()->setPosition(1500);
  QCOMPARE(listener.events, QStringList{"position 1500"});
  listener.events.clear();

  // Pause is not a stop.
  controller_->pause();
  QVERIFY(!listener.events.contains("stopped"));
  controller_->stop();
  QVERIFY(listener.events.contains("stopped"));

  // A backend switch replaces the player; its positions still arrive.
  controller_->setBackend(PlaybackBackendManager::Backend::GStreamer);
  listener.events.clear();
  player()->setPosition(2500);
  QCOMPARE(listener.events, QStringList{"position 2500"});
  listener.events.clear();

  host.removeListener(&listener);
  controller_->play();
  QVERIFY(listener.events.isEmpty());
}

void TestPlugins::playerHost_seekRunsAfterTheDelivery() {
  addSong();
  PluginPlayerHost host(*controller_);
  // The first listener seeks inside the delivery of 10000; the second must
  // still get 10000 first. The dummy player emits the position inside
  // setPosition, so a seek run at once would deliver 30000 to the second
  // listener before 10000.
  SeekingListener seeking(host, 10000, 30000);
  RecordingListener listener;
  host.addListener(&seeking);
  host.addListener(&listener);
  controller_->play();
  listener.events.clear();

  player()->setPosition(10000);
  QCOMPARE(listener.events, QStringList{"position 10000"});
  QTRY_COMPARE(listener.events, (QStringList{"position 10000", "seeked 30000",
                                             "position 30000"}));
  host.removeListener(&listener);
  host.removeListener(&seeking);
}

void TestPlugins::playerHost_dropsQueuedSeekWhenDestroyed() {
  addSong();
  PluginPlayerHost watching(*controller_);
  RecordingListener listener;
  watching.addListener(&listener);
  controller_->play();
  {
    PluginPlayerHost host(*controller_);
    host.seek(30000);
  }
  listener.events.clear();
  // The destroyed host was the queued seek's context, so it never runs.
  QTest::qWait(50);
  QVERIFY(listener.events.isEmpty());
  watching.removeListener(&listener);
}

void TestPlugins::playerHost_playResumesAfterTheCall() {
  addSong();
  PluginPlayerHost host(*controller_);
  controller_->play();
  controller_->pause();

  host.play();
  // Deferred: still paused right after the call.
  QCOMPARE(controller_->manager().getStatus(),
           PlaybackQueue::PlaybackStatus::Paused);
  QTRY_COMPARE(controller_->manager().getStatus(),
               PlaybackQueue::PlaybackStatus::Playing);
}

void TestPlugins::manager_loadsLyricsPlugin() {
  addSong();
  PluginManager manager(*controller_, pluginFolder(), dataRoot());
  // Its id and name from the metadata embedded in the library.
  const PluginEntry *found = entryWithId(manager, "lyrics");
  QVERIFY(found != nullptr);
  const PluginEntry &entry = *found;
  QCOMPARE(entry.state, PluginEntry::State::Loaded);
  QCOMPARE(entry.name, QString("Lyrics"));
  QVERIFY(entry.fileName.contains("lyricsplugin"));
  QObject *root = entry.root;
  auto *panels = qobject_cast<IPanelProvider *>(root);
  auto *pages = qobject_cast<ISettingsPageProvider *>(root);
  QVERIFY(panels != nullptr);
  QVERIFY(pages != nullptr);

  // Destroyed before the manager, as the info panel is.
  QWidget parent;
  QWidget *panel = panels->createPanel(&parent);
  QCOMPARE(panel->parentWidget(), &parent);
  auto *text = panel->findChild<QTextEdit *>();
  QVERIFY(text != nullptr);

  // The panel shows the tag lyrics of the song the host says started.
  controller_->play();
  QCOMPARE(text->toPlainText(), QString("tag"));
  controller_->stop();
  QVERIFY(text->toPlainText().isEmpty());

  IPluginSettingsPage *page = pages->createSettingsPage(&parent);
  QCOMPARE(page->widget()->parentWidget(), &parent);
}

void TestPlugins::lyricsSettings_loadSaveAndRestyleOpenPanels() {
  // Not an installed family: the offscreen platform has no fonts. Which font
  // the page's combo then selects is Qt's substitution, checked by hand.
  const QString family = "Stored Family";
  {
    QSettings settings;
    settings.setValue("lyrics/use_system_default_font", false);
    settings.setValue("lyrics/font_family", family);
    settings.setValue("lyrics/font_size", 20);
    settings.setValue("lyrics/highlight_color", "#ff0000");
  }

  PluginManager manager(*controller_, pluginFolder(), dataRoot());
  const PluginEntry *entry = entryWithId(manager, "lyrics");
  QVERIFY(entry != nullptr);
  QObject *root = entry->root;
  // Destroyed before the manager, as the info panel is.
  QWidget parent;
  QWidget *panel = qobject_cast<IPanelProvider *>(root)->createPanel(&parent);
  auto *text = panel->findChild<QTextEdit *>();
  QVERIFY(text != nullptr);

  // The stored family comes first, before the application font's families
  // (its CJK fallback on Windows), installed or not.
  QCOMPARE(text->font().families().front(), family);
  for (const QString &fallback : QApplication::font().families()) {
    QVERIFY(text->font().families().contains(fallback));
  }
  QCOMPARE(text->font().pointSize(), 20);

  // The page shows the stored values (the family is substituted, see above).
  IPluginSettingsPage *page =
      qobject_cast<ISettingsPageProvider *>(root)->createSettingsPage(&parent);
  QWidget *widget = page->widget();
  auto *familyCombo = widget->findChild<QFontComboBox *>();
  auto *sizeSpin = widget->findChild<QSpinBox *>();
  auto *systemFontCheck = widget->findChild<QCheckBox *>();
  auto *colorButton = widget->findChild<QPushButton *>();
  QVERIFY(familyCombo && sizeSpin && systemFontCheck && colorButton);
  QCOMPARE(sizeSpin->value(), 20);
  QVERIFY(!systemFontCheck->isChecked());
  QVERIFY(familyCombo->isEnabled());
  QCOMPARE(colorButton->text(), QString("#ff0000"));

  // The system font disables the family, which is still saved, as typed.
  systemFontCheck->setChecked(true);
  QVERIFY(!familyCombo->isEnabled());
  familyCombo->setEditText("Typed Family");
  sizeSpin->setValue(14);
  page->apply();
  QSettings settings;
  QCOMPARE(settings.value("lyrics/use_system_default_font").toBool(), true);
  QCOMPARE(settings.value("lyrics/font_family").toString(),
           QString("Typed Family"));
  QCOMPARE(settings.value("lyrics/font_size").toInt(), 14);
  QCOMPARE(settings.value("lyrics/highlight_color").toString(),
           QString("#ff0000"));

  // Applying restyles the open panel: the system font is the application
  // font, at the stored size.
  QCOMPARE(text->font().families(), QApplication::font().families());
  QCOMPARE(text->font().pointSize(), 14);
}

void TestPlugins::manager_skipsNonLibrariesAndDuplicateIds() {
  // Not a QTemporaryDir: the plugins loaded from it are never unloaded, and
  // on Windows a loaded DLL cannot be deleted until the process exits. This
  // folder is emptied on the next run instead.
  QDir folder(QDir::temp().filePath("music-player-tests/plugin-copies"));
  QVERIFY(folder.removeRecursively());
  QVERIFY(folder.mkpath("."));
  QFile notes(folder.filePath("notes.txt"));
  QVERIFY(notes.open(QIODevice::WriteOnly));
  notes.close();
  {
    // Not listed.
    PluginManager manager(*controller_, folder.path(), dataRoot());
    QVERIFY(manager.plugins().isEmpty());
  }

  // Two copies of the lyrics plugin under other names: the second has the
  // id of the first, and is skipped. Copies, so their root objects are not
  // the one manager_loadsLyricsPlugin initializes.
  const QDir built(QCoreApplication::applicationDirPath() + "/plugins");
  QString plugin;
  for (const QString &name : built.entryList(QDir::Files)) {
    if (QLibrary::isLibrary(name) && name.contains("lyricsplugin")) {
      plugin = built.filePath(name);
    }
  }
  QVERIFY(!plugin.isEmpty());
  const QString suffix = QFileInfo(plugin).suffix();
  QVERIFY(QFile::copy(plugin, folder.filePath("a." + suffix)));
  QVERIFY(QFile::copy(plugin, folder.filePath("b." + suffix)));
  PluginManager manager(*controller_, folder.path(), dataRoot());
  QCOMPARE(manager.plugins().size(), 2);
  const PluginEntry &first = manager.plugins().at(0);
  const PluginEntry &second = manager.plugins().at(1);
  QCOMPARE(first.fileName, "a." + suffix);
  QCOMPARE(first.state, PluginEntry::State::Loaded);
  QCOMPARE(second.fileName, "b." + suffix);
  QCOMPARE(second.state, PluginEntry::State::Skipped);
  QCOMPARE(second.reason, "same id as a." + suffix);
}

void TestPlugins::manager_listsDisabledPluginWithoutLoadingIt() {
  saveDisabledPluginIds({"lyrics"});
  PluginManager manager(*controller_, pluginFolder(), dataRoot());
  // Its name comes from the metadata, without loading it. That its code is
  // not loaded is not checked: other test functions load it first, and Qt
  // never unloads it.
  const PluginEntry *entry = entryWithId(manager, "lyrics");
  QVERIFY(entry != nullptr);
  QCOMPARE(entry->state, PluginEntry::State::Disabled);
  QCOMPARE(entry->name, QString("Lyrics"));
  QCOMPARE(entry->root, nullptr);
}

void TestPlugins::manager_registersSongFieldsAndRefusesClashes() {
  {
    PluginManager manager(*controller_, pluginFolder(), dataRoot());
    ColumnRegistry registry;
    SongLibrary library(registry, *databaseManager_);
    manager.registerSongFields(registry, library);
    QCOMPARE(library.providedSources().size(), size_t{1});
    QCOMPARE(library.providedSources().front().sourceId, QString("playstats"));
    QCOMPARE(registry.pluginFieldIds(),
             (QList<QString>{"play_count", "last_played_timestamp"}));
    const FieldDefinition *lastPlayed =
        registry.findField("last_played_timestamp");
    QCOMPARE(lastPlayed->valueType, ValueType::DateTime);
    QCOMPARE(lastPlayed->displayKind, DisplayKind::EpochSecondsDateTime);
    QVERIFY(!lastPlayed->writable);
    QVERIFY(!registry.findColumn("last_played_timestamp")->visibleByDefault);
  }
  {
    // A field of that name exists: the provider is refused as a whole.
    PluginManager manager(*controller_, pluginFolder(), dataRoot());
    ColumnRegistry registry;
    registry.addPluginField({.id = "last_played_timestamp", .title = "Other"},
                            {.id = "last_played_timestamp",
                             .source = ColumnSource::Plugin,
                             .valueType = ValueType::Number,
                             .writable = false});
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression("Plugin song fields refused: "
                                            "playstats .*already used"));
    SongLibrary library(registry, *databaseManager_);
    manager.registerSongFields(registry, library);
    QVERIFY(!registry.hasField("play_count"));
    QVERIFY(library.providedSources().empty());
  }
}

void TestPlugins::playStats_valuesChangedIsDeferredAndDataStaysInTheDataRoot() {
  addSong();
  PluginManager manager(*controller_, pluginFolder(), dataRoot());
  // Loading the plugins created no file.
  QVERIFY(!QFileInfo::exists(dataRoot()));
  QSignalSpy changedSpy(&manager, &PluginManager::providedValuesChanged);

  controller_->play();
  // Reported inside the song start, run from the event loop.
  QCOMPARE(changedSpy.count(), 0);
  QTRY_COMPARE(changedSpy.count(), 1);
  QCOMPARE(changedSpy.at(0).at(0).toString(), QString("playstats"));
  QCOMPARE(changedSpy.at(0).at(1).toStringList(),
           QStringList{"song|artist|album"});
  // The plugin's file, in its folder under the data root.
  QVERIFY(QFileInfo::exists(dataRoot() + "/playstats/playstats.db"));
}

QTEST_MAIN(TestPlugins)
#include "tst_plugins.moc"
