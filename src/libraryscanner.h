#ifndef LIBRARYSCANNER_H
#define LIBRARYSCANNER_H

#include "columnregistry.h"
#include "songlibrary.h"
#include <QHash>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// The worker-thread part of a monitored library scan: availability check,
// walk, classification and parse (steps 2-5 of "Scan" in
// docs/monitored-library.md). Input and output are plain data, so it runs on
// any thread and does not touch SongLibrary or the database. MonitoredLibrary
// builds the input on the main thread and applies the result there.

// A file's stats as stored in `monitored_files`: last_write_time in
// nanoseconds since its clock's epoch, and size in bytes. Compared for exact
// equality.
struct FileStats {
  qint64 mtimeNs = 0;
  qint64 size = 0;
  bool operator==(const FileStats &) const = default;
};

// The stats of the regular file at path (not following a final symlink), or
// nullopt when it cannot be read.
std::optional<FileStats> readFileStats(const QString &path);

// Whether path is root or under it, by whole path components. Compares
// case-insensitively on macOS and Windows.
bool isSameOrUnderPath(const QString &path, const QString &root);

// Parses one file with a ColumnRegistry; returns nullopt when the file cannot
// be read. Defaults to SongParser::parse.
using ScanParseFn = std::function<std::optional<MSong>(const std::string &,
                                                       const ColumnRegistry &)>;

struct ScanFolderInput {
  int folderId = -1;
  // Canonical root path.
  QString path;
  // Scopes to scan under the root: the root itself for a full scan, or paths
  // under it (files or directory subtrees). Only snapshot entries inside a
  // scope can count as gone.
  QStringList scopes;
  // Whether the library has files for this folder; an empty root is offline
  // only then.
  bool hasFiles = false;
  // Skip the availability check ("Remove missing songs"): the folder is
  // treated as online, so every file not found counts as gone.
  bool skipAvailabilityCheck = false;
};

struct ScanSnapshotEntry {
  int pk = -1;
  int folderId = -1;
  FileStats stats;
};

struct LibraryScanInput {
  std::vector<ScanFolderInput> folders;
  // Monitored files inside the scopes, by canonical path.
  QHash<QString, ScanSnapshotEntry> snapshot;
  // Unreadable files inside the scopes, by path, with the stats they failed
  // with.
  QHash<QString, FileStats> failed;
  // A copy: the live one may change on the main thread meanwhile.
  ColumnRegistry registry;
};

struct ScanEntry {
  enum class Kind { New, Changed, Gone };
  Kind kind = Kind::New;
  int folderId = -1;
  QString path;
  // Changed and Gone: the snapshot entry.
  int pk = -1;
  FileStats snapshotStats;
  // New and Changed: the file's stats and parsed song (not prepared yet; see
  // SongLibrary::prepareParsedSong).
  FileStats diskStats;
  MSong song;
};

struct LibraryScanResult {
  struct Availability {
    int folderId = -1;
    bool online = true;
  };
  std::vector<Availability> availability;
  std::vector<ScanEntry> entries;
  // Files that failed to parse in this scan.
  QHash<QString, FileStats> newFailed;
  // Previously failed paths that are gone or now parse.
  QStringList clearedFailed;
};

// Called with (parsed, toParse) after classification and after each parse.
using ScanProgressFn = std::function<void(int done, int total)>;

// Runs the scan. parse may be empty (SongParser::parse).
LibraryScanResult scanLibrary(const LibraryScanInput &input,
                              const ScanParseFn &parse,
                              const ScanProgressFn &progress = {});

#endif // LIBRARYSCANNER_H
