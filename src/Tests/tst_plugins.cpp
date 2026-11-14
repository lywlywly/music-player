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
#include <QSettings>
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
#include "../pluginapi/iplayerlistener.h"
#include "../pluginapi/iplugin.h"
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
  song.insert_or_assign(
      "song_identity_key",
      FieldValue(title.toLower().toStdString() + "|artist|album",
                 "song_identity_key"));
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
  void positionChanged(qint64 positionMs) override {
    events << "position " + QString::number(positionMs);
  }

  QStringList events;
};
} // namespace

class TestPlugins : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void playerHost_forwardsStartPositionAndStop();
  void manager_loadsLyricsPlugin();
  void lyricsSettings_loadSaveAndRestyleOpenPanels();
  void manager_skipsNonLibrariesAndDuplicateIds();

private:
  DummyAudioPlayer *player() const;
  // Adds a song to the playlist and makes it the one Play starts.
  void addSong();

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

void TestPlugins::manager_loadsLyricsPlugin() {
  addSong();
  PluginManager manager(*controller_,
                        QCoreApplication::applicationDirPath() + "/plugins");
  QCOMPARE(manager.plugins().size(), 1);
  QObject *root = manager.plugins().front();
  QCOMPARE(qobject_cast<IPlugin *>(root)->id(), QString("lyrics"));
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

  PluginManager manager(*controller_,
                        QCoreApplication::applicationDirPath() + "/plugins");
  QObject *root = manager.plugins().front();
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
    PluginManager manager(*controller_, folder.path());
    QVERIFY(manager.plugins().isEmpty());
  }

  // Two copies of the lyrics plugin under other names: the second has the
  // id of the first. Copies, so their root objects are not the one
  // manager_loadsLyricsPlugin initializes.
  const QDir built(QCoreApplication::applicationDirPath() + "/plugins");
  QString plugin;
  for (const QString &name : built.entryList(QDir::Files)) {
    if (QLibrary::isLibrary(name)) {
      plugin = built.filePath(name);
    }
  }
  QVERIFY(!plugin.isEmpty());
  const QString suffix = QFileInfo(plugin).suffix();
  QVERIFY(QFile::copy(plugin, folder.filePath("a." + suffix)));
  QVERIFY(QFile::copy(plugin, folder.filePath("b." + suffix)));
  PluginManager manager(*controller_, folder.path());
  QCOMPARE(manager.plugins().size(), 1);
  QCOMPARE(qobject_cast<IPlugin *>(manager.plugins().front())->id(),
           QString("lyrics"));
}

QTEST_MAIN(TestPlugins)
#include "tst_plugins.moc"
