#ifndef MONITOREDLIBRARY_H
#define MONITOREDLIBRARY_H

#include "libraryscanner.h"
#include <QHash>
#include <QObject>
#include <QString>
#include <map>
#include <optional>
#include <vector>

class ColumnRegistry;
class DatabaseManager;
class SongLibrary;
template <typename T> class QFutureWatcher;

// The monitored library: watched folders (`library_folders`) and the songs
// found under them (`monitored_files`). Scans keep it in sync with the disk;
// see docs/monitored-library.md.
//
// It never deletes song rows and does not know about playlists. Songs that
// leave it are announced with songsLeft; PlaylistCollection then releases
// those not held by the manual library.
//
// One scan runs at a time, on a worker thread. Requests made meanwhile are
// merged into one follow-up scan. Results are applied on the main thread in
// batches, one transaction each.
class MonitoredLibrary : public QObject {
  Q_OBJECT
public:
  struct Folder {
    int id = -1;
    // Canonical path.
    QString path;
    // Offline: the root is missing or unreadable, or is empty while the
    // library has files for it. In memory only; re-evaluated by every scan.
    bool online = true;
  };

  struct AddFolderResult {
    // -1 on failure.
    int folderId = -1;
    QString error;
  };

  // parse defaults to SongParser::parse; tests inject one.
  MonitoredLibrary(SongLibrary &library, const ColumnRegistry &registry,
                   DatabaseManager &databaseManager, ScanParseFn parse = {},
                   QObject *parent = nullptr);
  ~MonitoredLibrary() override;

  // Reads `library_folders` and `monitored_files`. Call once, after the song
  // library is loaded and before PlaylistCollection::loadAll. Emits no
  // signals.
  void load();

  // Canonicalises path and adds it as a watched folder, then requests a scan
  // of it. Fails when path is not an existing directory, or is inside, or
  // contains, a watched folder (by path component; case-insensitively on
  // macOS and Windows).
  AddFolderResult addFolder(const QString &path);
  // Removes the folder and its `monitored_files` rows; emits songsLeft for its
  // songs. A running scan is not cancelled; its results for the folder are
  // dropped. Returns false for an unknown id.
  bool removeFolder(int folderId);
  // Watched folders, by path.
  std::vector<Folder> folders() const;

  // Requests a full scan of every watched folder.
  void rescan();
  // Requests a scan of the folder that skips the availability check, so every
  // file not found counts as gone. For a folder offline because its root is
  // empty, when its files really were deleted.
  void removeMissingSongs(int folderId);
  // Requests a scan of paths (files or directories). Paths under no watched
  // folder are ignored.
  void scanPaths(const QStringList &paths);
  bool isScanning() const;

  bool holds(int songPk) const;
  // Monitored song pks, ascending.
  std::vector<int> songPks() const;
  // After the app wrote the file's tags (Song Properties): if songPk is
  // monitored, stores the file's new stats, so the next scan does not
  // re-parse it.
  void noteSongWritten(int songPk);

  // Rows applied per event-loop turn.
  static constexpr int kApplyBatchSize = 200;

signals:
  // Songs that joined the monitored library (new rows, or existing rows,
  // refreshed from the file).
  void songsEntered(const std::vector<int> &songPks);
  // Monitored songs re-read because their file changed.
  void songsUpdated(const std::vector<int> &songPks);
  // Songs that left the monitored library; their rows may survive.
  void songsLeft(const std::vector<int> &songPks);
  void folderAvailabilityChanged(int folderId, bool online);
  // done / total files to parse in the running scan.
  void scanProgress(int done, int total);
  void scanFinished();

private:
  struct Entry {
    int folderId = -1;
    QString path;
    FileStats stats;
  };
  // Pending scan scopes of one folder.
  struct Request {
    bool full = false;
    bool skipAvailabilityCheck = false;
    QStringList paths;
  };

  void requestFull(int folderId, bool skipAvailabilityCheck = false);
  void requestPath(int folderId, const QString &path);
  void startNextScan();
  void onScanFinished();
  void applyNextBatch();
  void finishApply();
  const Folder *findFolder(int folderId) const;
  Folder *findFolder(int folderId);

  SongLibrary &library_;
  const ColumnRegistry &registry_;
  DatabaseManager &databaseManager_;
  ScanParseFn parse_;

  std::vector<Folder> folders_;
  std::map<int, Entry> entries_;
  QHash<QString, int> pkByPath_;
  QHash<QString, FileStats> failed_;

  std::map<int, Request> pending_;
  QFutureWatcher<LibraryScanResult> *scanWatcher_ = nullptr;
  // The result being applied, and the next entry to apply.
  std::optional<LibraryScanResult> applying_;
  size_t applyIndex_ = 0;
};

#endif // MONITOREDLIBRARY_H
