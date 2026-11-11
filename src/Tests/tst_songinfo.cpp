#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../dummyfilesystemmonitor.h"
#include "../monitoredlibrary.h"
#include "../playlist.h"
#include "../playlistcollection.h"
#include "../songinfo.h"
#include "../songinfopanel.h"
#include "../songinfopresenter.h"
#include "../songlibrary.h"

namespace {
// Every file parses as a song titled by its base name, with fixed audio
// properties.
std::optional<MSong> fakeParse(const std::string &path) {
  const QString qpath = QString::fromStdString(path);
  if (!QFileInfo::exists(qpath)) {
    return std::nullopt;
  }
  MSong song;
  const auto set = [&song](const char *field, const std::string &value) {
    song.insert_or_assign(field, FieldValue(value, field));
  };
  set("title", QFileInfo(qpath).baseName().toStdString());
  set("artist", "Artist");
  set("album", "Album");
  set("codec", "MP3");
  set("bitrate", "320");
  set("sample_rate", "44100");
  set("duration", "125");
  set("filepath", path);
  return song;
}

bool writeFile(const QString &path, const QByteArray &content) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return file.write(content) == content.size();
}

const SongInfoRow *findRow(const SongInfo &info, const QString &label) {
  for (const SongInfoSection &section : info.sections) {
    for (const SongInfoRow &row : section.rows) {
      if (row.label == label) {
        return &row;
      }
    }
  }
  return nullptr;
}

QString valueOf(const SongInfo &info, const QString &label) {
  const SongInfoRow *row = findRow(info, label);
  return row != nullptr ? row->value : QString();
}
} // namespace

class TestSongInfo : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void build_showsFileAudioTagAndLibraryRows();
  void build_missingFile_showsNotFound();
  void offlineFolder_skipsTheFileAndRefreshesThePanel();
  void build_removedSong_isEmpty();

  void presenter_showsSelectedSongAfterDebounce();
  void presenter_rebuildsOnlyForItsRow();

  void panel_buttonsFollowSongAndWidth();

private:
  QString root() const;
  // Adds the file to the manual playlist and returns its pk.
  int addManual(const QString &path);
  void waitIdle();
  QTimer *rebuildTimer(SongInfoPresenter &presenter) const;

  QTemporaryDir *dir_ = nullptr;
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  DummyFileSystemMonitor *monitor_ = nullptr;
  MonitoredLibrary *monitored_ = nullptr;
  PlaylistCollection *collection_ = nullptr;
  Playlist *playlist_ = nullptr;
  QString connectionName_;
};

void TestSongInfo::init() {
  qRegisterMetaType<std::vector<int>>();
  dir_ = new QTemporaryDir();
  QVERIFY(dir_->isValid());
  connectionName_ =
      QStringLiteral("test_songinfo_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ =
      new SongLibrary(*registry_, *databaseManager_,
                      [](const std::string &path, const ColumnRegistry &,
                         std::unordered_map<std::string, std::string> *) {
                        return fakeParse(path);
                      });
  monitor_ = new DummyFileSystemMonitor();
  monitored_ = new MonitoredLibrary(
      *library_, *registry_, *databaseManager_,
      [](const std::string &path, const ColumnRegistry &) {
        return fakeParse(path);
      },
      monitor_);
  monitored_->load();
  collection_ =
      new PlaylistCollection(*library_, *databaseManager_, *monitored_);
  collection_->loadAll();
  playlist_ = collection_->playlist(collection_->playlistIdsInOrder().front());
  QVERIFY(playlist_ != nullptr);
}

void TestSongInfo::cleanup() {
  waitIdle();
  delete collection_;
  collection_ = nullptr;
  playlist_ = nullptr;
  delete monitored_;
  monitored_ = nullptr;
  delete monitor_;
  monitor_ = nullptr;
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
  delete dir_;
  dir_ = nullptr;
}

QString TestSongInfo::root() const {
  return QFileInfo(dir_->path()).canonicalFilePath();
}

int TestSongInfo::addManual(const QString &path) {
  const std::vector<int> pks = playlist_->addFiles({path.toStdString()});
  return pks.size() == 1 ? pks.front() : -1;
}

void TestSongInfo::waitIdle() {
  QTRY_VERIFY_WITH_TIMEOUT(!monitored_->isScanning(), 5000);
}

QTimer *TestSongInfo::rebuildTimer(SongInfoPresenter &presenter) const {
  return presenter.findChild<QTimer *>();
}

void TestSongInfo::build_showsFileAudioTagAndLibraryRows() {
  const QString path = root() + "/w/Song A.mp3";
  QVERIFY(writeFile(path, QByteArray(1234, 'x')));
  const QDateTime modified =
      QDateTime::fromString("2025-03-04T05:06:07", Qt::ISODate);
  {
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
  }
  QVERIFY(monitored_->addFolder(root() + "/w").folderId > 0);
  waitIdle();
  const int pk = monitored_->songPks().front();

  const SongInfo info = buildSongInfo(pk, *library_, *monitored_, *registry_);
  QCOMPARE(info.songPk, pk);
  QCOMPARE(valueOf(info, "File name"), QString("Song A.mp3"));
  QCOMPARE(valueOf(info, "Folder"), QDir::toNativeSeparators(root() + "/w"));
  QCOMPARE(valueOf(info, "Path"), QDir::toNativeSeparators(path));
  QCOMPARE(valueOf(info, "Size"), QLocale().formattedDataSize(1234));
  QCOMPARE(findRow(info, "Size")->toolTip,
           QString("%1 bytes").arg(QLocale().toString(1234)));
  QCOMPARE(valueOf(info, "Modified"), QString("2025-03-04 05:06:07"));
  QVERIFY(!valueOf(info, "Created").isEmpty());
  QCOMPARE(findRow(info, "Status"), nullptr);

  QCOMPARE(valueOf(info, "Codec"), QString("MP3"));
  QCOMPARE(valueOf(info, "Bitrate"), QString("320 kbps"));
  QCOMPARE(valueOf(info, "Sample rate"), QString("44100 Hz"));
  QCOMPARE(valueOf(info, "Duration"),
           library_->getSongByPK(pk).at("duration").display());
  QCOMPARE(valueOf(info, "Title"), QString("Song A"));
  QCOMPARE(valueOf(info, "Artist"), QString("Artist"));
  // A field the song does not have gets no row.
  QCOMPARE(findRow(info, "Genre"), nullptr);
  QCOMPARE(valueOf(info, "Watched folder"),
           QDir::toNativeSeparators(root() + "/w"));

  // A song only in a manual playlist has no Watched folder row.
  const QString manualPath = root() + "/manual.mp3";
  QVERIFY(writeFile(manualPath, "m"));
  const int manualPk = addManual(manualPath);
  QVERIFY(manualPk >= 0);
  const SongInfo manual =
      buildSongInfo(manualPk, *library_, *monitored_, *registry_);
  QVERIFY(findRow(manual, "Size") != nullptr);
  QCOMPARE(findRow(manual, "Watched folder"), nullptr);
}

void TestSongInfo::build_missingFile_showsNotFound() {
  const QString path = root() + "/gone.mp3";
  QVERIFY(writeFile(path, "g"));
  const int pk = addManual(path);
  QVERIFY(pk >= 0);
  QVERIFY(QFile::remove(path));

  const SongInfo info = buildSongInfo(pk, *library_, *monitored_, *registry_);
  QVERIFY(!info.isEmpty());
  QCOMPARE(valueOf(info, "Status"), QString("File not found"));
  QCOMPARE(valueOf(info, "File name"), QString("gone.mp3"));
  QCOMPARE(findRow(info, "Size"), nullptr);
  QCOMPARE(findRow(info, "Modified"), nullptr);
  // The stored values are still shown.
  QCOMPARE(valueOf(info, "Title"), QString("gone"));
}

void TestSongInfo::offlineFolder_skipsTheFileAndRefreshesThePanel() {
  QVERIFY(writeFile(root() + "/w/a.mp3", "a"));
  QVERIFY(monitored_->addFolder(root() + "/w").folderId > 0);
  waitIdle();
  const int pk = monitored_->songPks().front();
  SongInfoPanel panel;
  panel.show();
  SongInfoPresenter presenter(&panel, *library_, *monitored_, *registry_);
  presenter.showSong(playlist_, pk);
  QTRY_COMPARE(panel.info().songPk, pk);
  QCOMPARE(findRow(panel.info(), "Status"), nullptr);

  // Unmount: the folder's root is gone, so the folder is offline.
  QVERIFY(QDir().rename(root() + "/w", root() + "/unmounted"));
  monitored_->rescan();
  waitIdle();
  QVERIFY(!monitored_->folderOf(pk)->online);
  // The file is back at its path, but an offline folder is not looked at.
  QVERIFY(writeFile(root() + "/w/a.mp3", "a"));

  const SongInfo info = buildSongInfo(pk, *library_, *monitored_, *registry_);
  QCOMPARE(valueOf(info, "Status"), QString("Folder offline"));
  QCOMPARE(findRow(info, "Size"), nullptr);
  QCOMPARE(
      valueOf(info, "Watched folder"),
      QString("%1 (offline)").arg(QDir::toNativeSeparators(root() + "/w")));
  // The panel follows the folder's availability without a selection change.
  QTRY_COMPARE(valueOf(panel.info(), "Status"), QString("Folder offline"));
}

void TestSongInfo::build_removedSong_isEmpty() {
  const QString path = root() + "/r.mp3";
  QVERIFY(writeFile(path, "r"));
  const int pk = addManual(path);
  QVERIFY(pk >= 0);
  // In no other playlist and not monitored, so it leaves the library.
  playlist_->removeSongs({0});
  QVERIFY(library_->getSongByPK(pk).empty());

  QVERIFY(buildSongInfo(pk, *library_, *monitored_, *registry_).isEmpty());
  QVERIFY(buildSongInfo(-1, *library_, *monitored_, *registry_).isEmpty());
}

void TestSongInfo::presenter_showsSelectedSongAfterDebounce() {
  QVERIFY(writeFile(root() + "/a.mp3", "a"));
  QVERIFY(writeFile(root() + "/b.mp3", "b"));
  const int a = addManual(root() + "/a.mp3");
  const int b = addManual(root() + "/b.mp3");
  SongInfoPanel panel;
  SongInfoPresenter presenter(&panel, *library_, *monitored_, *registry_);
  QVERIFY(panel.info().isEmpty());

  // Two changes in quick succession: one rebuild, for the last.
  presenter.showSong(playlist_, a);
  presenter.showSong(playlist_, b);
  QVERIFY(panel.info().isEmpty());
  QVERIFY(rebuildTimer(presenter)->isActive());
  QTRY_COMPARE(panel.info().songPk, b);
  QCOMPARE(valueOf(panel.info(), "Title"), QString("b"));

  presenter.showSong(playlist_, -1);
  QTRY_VERIFY(panel.info().isEmpty());
}

void TestSongInfo::presenter_rebuildsOnlyForItsRow() {
  QVERIFY(writeFile(root() + "/a.mp3", "a"));
  QVERIFY(writeFile(root() + "/b.mp3", "b"));
  const int a = addManual(root() + "/a.mp3");
  const int b = addManual(root() + "/b.mp3");
  SongInfoPanel panel;
  SongInfoPresenter presenter(&panel, *library_, *monitored_, *registry_);
  presenter.showSong(playlist_, a);
  QTRY_COMPARE(panel.info().songPk, a);
  QCOMPARE(findRow(panel.info(), "Play Count"), nullptr);

  QVERIFY(library_->incrementPlayCount(b));
  collection_->notifySongChanged(b);
  QVERIFY(!rebuildTimer(presenter)->isActive());

  QVERIFY(library_->incrementPlayCount(a));
  collection_->notifySongChanged(a);
  QVERIFY(rebuildTimer(presenter)->isActive());
  QTRY_COMPARE(valueOf(panel.info(), "Play Count"), QString("1"));
}

void TestSongInfo::panel_buttonsFollowSongAndWidth() {
  QVERIFY(writeFile(root() + "/a.mp3", "a"));
  const int a = addManual(root() + "/a.mp3");
  SongInfoPanel panel;
  panel.resize(600, 400);
  panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));
  const QList<QPushButton *> buttons = panel.findChildren<QPushButton *>();
  QCOMPARE(buttons.size(), 2);
  QPushButton *reveal = buttons.at(0);
  QPushButton *edit = buttons.at(1);
  QCOMPARE(reveal->text(), QString("Open Containing Folder"));
  QCOMPARE(edit->text(), QString("Edit Properties..."));
  QVERIFY(!reveal->isEnabled());
  QVERIFY(!edit->isEnabled());

  panel.setInfo(buildSongInfo(a, *library_, *monitored_, *registry_));
  QVERIFY(reveal->isEnabled());
  QVERIFY(edit->isEnabled());
  QSignalSpy revealSpy(&panel, &SongInfoPanel::revealRequested);
  QSignalSpy editSpy(&panel, &SongInfoPanel::editPropertiesRequested);
  reveal->click();
  edit->click();
  QCOMPARE(revealSpy.count(), 1);
  QCOMPARE(revealSpy.at(0).at(0).toInt(), a);
  QCOMPARE(editSpy.count(), 1);
  QCOMPARE(editSpy.at(0).at(0).toInt(), a);

  // Side by side when both fit, stacked in a narrow column.
  QTRY_COMPARE(edit->y(), reveal->y());
  panel.resize(150, 400);
  QTRY_VERIFY(edit->y() > reveal->y());
  QVERIFY(panel.width() <= 150);

  panel.setInfo({});
  QVERIFY(!reveal->isEnabled());
  QVERIFY(!edit->isEnabled());
}

QTEST_MAIN(TestSongInfo)
#include "tst_songinfo.moc"
