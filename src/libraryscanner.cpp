#include "libraryscanner.h"

#include "audiofileformats.h"
#include "songparser.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace {
fs::path toFsPath(const QString &path) {
  return fs::path(path.toStdU16String());
}

// On macOS, names come as stored, which can be decomposed (NFD); SongLibrary
// stores Qt's composed (NFC) paths. See "Scan" in docs/monitored-library.md.
QString fromFsPath(const fs::path &path) {
  const QString qpath = QString::fromStdU16String(path.generic_u16string());
#ifdef Q_OS_MACOS
  return qpath.normalized(QString::NormalizationForm_C);
#else
  return qpath;
#endif
}

constexpr Qt::CaseSensitivity kPathCase =
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    Qt::CaseInsensitive;
#else
    Qt::CaseSensitive;
#endif

bool isHiddenName(const QString &name) {
  return name.startsWith(QLatin1Char('.'));
}

// Whether a path component of path below root is hidden.
bool hasHiddenComponentBelow(const QString &path, const QString &root) {
  if (path.size() <= root.size()) {
    return false;
  }
  const QString relative =
      path.mid(root.endsWith(QLatin1Char('/')) ? root.size() : root.size() + 1);
  const QStringList parts =
      relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  return std::any_of(parts.begin(), parts.end(), isHiddenName);
}

bool isInScopes(const QString &path, const QStringList &scopes) {
  return std::any_of(scopes.begin(), scopes.end(), [&](const QString &scope) {
    return isSameOrUnderPath(path, scope);
  });
}

void addIfAudioFile(const fs::path &path, QHash<QString, FileStats> &onDisk) {
  const QString qpath = fromFsPath(path);
  if (!hasAudioFileSuffix(qpath)) {
    return;
  }
  if (std::optional<FileStats> stats = readFileStats(qpath)) {
    onDisk.insert(qpath, *stats);
  }
}

// Lists the regular audio files in scope (a file, or a directory subtree),
// without following symlinks and skipping hidden entries.
void walkScope(const QString &root, const QString &scope,
               QHash<QString, FileStats> &onDisk) {
  if (hasHiddenComponentBelow(scope, root)) {
    return;
  }
  const fs::path scopePath = toFsPath(scope);
  std::error_code ec;
  const fs::file_status status = fs::symlink_status(scopePath, ec);
  if (ec) {
    return;
  }
  if (fs::is_regular_file(status)) {
    addIfAudioFile(scopePath, onDisk);
    return;
  }
  if (!fs::is_directory(status)) {
    return;
  }
  // Does not follow directory symlinks (no follow_directory_symlink).
  fs::recursive_directory_iterator it(
      scopePath, fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    return;
  }
  for (const fs::recursive_directory_iterator end; it != end;
       it.increment(ec)) {
    if (ec) {
      break;
    }
    const fs::directory_entry &entry = *it;
    const QString name = fromFsPath(entry.path().filename());
    std::error_code statusEc;
    const fs::file_status entryStatus = entry.symlink_status(statusEc);
    if (statusEc) {
      continue;
    }
    if (isHiddenName(name)) {
      if (fs::is_directory(entryStatus)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (fs::is_regular_file(entryStatus)) {
      addIfAudioFile(entry.path(), onDisk);
    }
  }
}

bool isFolderOnline(const ScanFolderInput &folder) {
  return folder.skipAvailabilityCheck ||
         isFolderRootAvailable(folder.path, folder.hasFiles);
}

struct ParseJob {
  ScanEntry::Kind kind = ScanEntry::Kind::New;
  int folderId = -1;
  QString path;
  int pk = -1;
  FileStats snapshotStats;
  FileStats diskStats;
};
} // namespace

std::optional<FileStats> readFileStats(const QString &path) {
  const fs::path fsPath = toFsPath(path);
  std::error_code ec;
  const fs::file_status status = fs::symlink_status(fsPath, ec);
  if (ec || !fs::is_regular_file(status)) {
    return std::nullopt;
  }
  const std::uintmax_t size = fs::file_size(fsPath, ec);
  if (ec) {
    return std::nullopt;
  }
  const fs::file_time_type mtime = fs::last_write_time(fsPath, ec);
  if (ec) {
    return std::nullopt;
  }
  return FileStats{.mtimeNs = static_cast<qint64>(
                       std::chrono::duration_cast<std::chrono::nanoseconds>(
                           mtime.time_since_epoch())
                           .count()),
                   .size = static_cast<qint64>(size)};
}

// See "Offline folders" in docs/monitored-library.md.
bool isFolderRootAvailable(const QString &root, bool hasFiles) {
  const fs::path rootPath = toFsPath(root);
  std::error_code ec;
  if (!fs::is_directory(fs::status(rootPath, ec)) || ec) {
    return false;
  }
  fs::directory_iterator it(rootPath, ec);
  if (ec) {
    return false;
  }
  return !hasFiles || it != fs::directory_iterator();
}

bool isSameOrUnderPath(const QString &path, const QString &root) {
  if (path.compare(root, kPathCase) == 0) {
    return true;
  }
  const QString prefix =
      root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
  return path.startsWith(prefix, kPathCase);
}

LibraryScanResult scanLibrary(const LibraryScanInput &input,
                              const ScanParseFn &parse,
                              const ScanProgressFn &progress) {
  LibraryScanResult result;
  std::vector<ParseJob> jobs;

  for (const ScanFolderInput &folder : input.folders) {
    const bool online = isFolderOnline(folder);
    result.availability.push_back({folder.folderId, online});
    if (!online) {
      continue;
    }

    QHash<QString, FileStats> onDisk;
    for (const QString &scope : folder.scopes) {
      walkScope(folder.path, scope, onDisk);
    }

    for (auto it = input.snapshot.cbegin(); it != input.snapshot.cend(); ++it) {
      const ScanSnapshotEntry &stored = it.value();
      if (stored.folderId != folder.folderId ||
          !isInScopes(it.key(), folder.scopes)) {
        continue;
      }
      const auto disk = onDisk.constFind(it.key());
      if (disk == onDisk.cend()) {
        ScanEntry gone;
        gone.kind = ScanEntry::Kind::Gone;
        gone.folderId = folder.folderId;
        gone.path = it.key();
        gone.pk = stored.pk;
        gone.snapshotStats = stored.stats;
        result.entries.push_back(std::move(gone));
      } else if (disk.value() != stored.stats) {
        jobs.push_back({.kind = ScanEntry::Kind::Changed,
                        .folderId = folder.folderId,
                        .path = it.key(),
                        .pk = stored.pk,
                        .snapshotStats = stored.stats,
                        .diskStats = disk.value()});
      }
    }

    for (auto it = onDisk.cbegin(); it != onDisk.cend(); ++it) {
      if (input.snapshot.contains(it.key())) {
        continue;
      }
      const auto failed = input.failed.constFind(it.key());
      if (failed != input.failed.cend() && failed.value() == it.value()) {
        continue;
      }
      jobs.push_back({.kind = ScanEntry::Kind::New,
                      .folderId = folder.folderId,
                      .path = it.key(),
                      .diskStats = it.value()});
    }

    for (auto it = input.failed.cbegin(); it != input.failed.cend(); ++it) {
      if (isSameOrUnderPath(it.key(), folder.path) &&
          isInScopes(it.key(), folder.scopes) && !onDisk.contains(it.key())) {
        result.clearedFailed.push_back(it.key());
      }
    }
  }

  std::sort(jobs.begin(), jobs.end(), [](const ParseJob &a, const ParseJob &b) {
    return a.path < b.path;
  });
  const int total = static_cast<int>(jobs.size());
  if (progress) {
    progress(0, total);
  }
  int done = 0;
  for (const ParseJob &job : jobs) {
    std::optional<MSong> song =
        parse ? parse(job.path.toStdString(), input.registry)
              : SongParser::parse(job.path.toStdString(), input.registry);
    if (!song) {
      result.newFailed.insert(job.path, job.diskStats);
      if (job.kind == ScanEntry::Kind::Changed) {
        ScanEntry gone;
        gone.kind = ScanEntry::Kind::Gone;
        gone.folderId = job.folderId;
        gone.path = job.path;
        gone.pk = job.pk;
        gone.snapshotStats = job.snapshotStats;
        result.entries.push_back(std::move(gone));
      }
    } else {
      if (input.failed.contains(job.path)) {
        result.clearedFailed.push_back(job.path);
      }
      ScanEntry entry;
      entry.kind = job.kind;
      entry.folderId = job.folderId;
      entry.path = job.path;
      entry.pk = job.pk;
      entry.snapshotStats = job.snapshotStats;
      entry.diskStats = job.diskStats;
      entry.song = std::move(*song);
      result.entries.push_back(std::move(entry));
    }
    ++done;
    if (progress) {
      progress(done, total);
    }
  }

  std::sort(
      result.entries.begin(), result.entries.end(),
      [](const ScanEntry &a, const ScanEntry &b) { return a.path < b.path; });
  return result;
}
