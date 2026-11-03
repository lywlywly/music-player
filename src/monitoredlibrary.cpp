#include "monitoredlibrary.h"

#include "columnregistry.h"
#include "databasemanager.h"
#include "ifilesystemmonitor.h"
#include "songlibrary.h"
#include <QDebug>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPromise>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>
#include <utility>

namespace {
void execOrFatal(QSqlQuery &q, const char *context) {
  if (!q.exec()) {
    qFatal("%s: %s", context, q.lastError().text().toUtf8().data());
  }
}
} // namespace

MonitoredLibrary::MonitoredLibrary(SongLibrary &library,
                                   const ColumnRegistry &registry,
                                   DatabaseManager &databaseManager,
                                   ScanParseFn parse,
                                   IFileSystemMonitor *monitor, QObject *parent)
    : QObject(parent), library_{library}, registry_{registry},
      databaseManager_{databaseManager}, parse_{std::move(parse)},
      monitor_{monitor}, watchDebounceTimer_{new QTimer(this)},
      offlinePollTimer_{new QTimer(this)} {
  watchDebounceTimer_->setSingleShot(true);
  watchDebounceTimer_->setInterval(kWatchDebounceMs);
  connect(watchDebounceTimer_, &QTimer::timeout, this,
          [this]() { scanPaths(std::exchange(changedPaths_, {})); });
  offlinePollTimer_->setInterval(kOfflinePollMs);
  connect(offlinePollTimer_, &QTimer::timeout, this,
          &MonitoredLibrary::checkOfflineFolders);
  if (monitor_) {
    connect(monitor_, &IFileSystemMonitor::pathsChanged, this,
            &MonitoredLibrary::onPathsChanged);
    connect(monitor_, &IFileSystemMonitor::folderNeedsRescan, this,
            &MonitoredLibrary::onFolderNeedsRescan);
  }
}

MonitoredLibrary::~MonitoredLibrary() = default;

void MonitoredLibrary::load() {
  folders_.clear();
  entries_.clear();
  pkByPath_.clear();

  QSqlQuery q(databaseManager_.db());
  if (!q.exec("SELECT folder_id, path FROM library_folders ORDER BY path")) {
    qFatal("MonitoredLibrary::load folders: %s",
           q.lastError().text().toUtf8().data());
  }
  while (q.next()) {
    folders_.push_back(
        Folder{.id = q.value(0).toInt(), .path = q.value(1).toString()});
  }

  if (!q.exec("SELECT song_id, folder_id, mtime, size FROM monitored_files")) {
    qFatal("MonitoredLibrary::load files: %s",
           q.lastError().text().toUtf8().data());
  }
  while (q.next()) {
    const int pk = q.value(0).toInt();
    const MSong &song = library_.getSongByPK(pk);
    const auto pathIt = song.find("filepath");
    if (pathIt == song.end()) {
      qFatal("MonitoredLibrary::load: monitored song %d is not in the library",
             pk);
    }
    const QString path = QString::fromStdString(pathIt->second.text);
    entries_[pk] = Entry{.folderId = q.value(1).toInt(),
                         .path = path,
                         .stats = FileStats{.mtimeNs = q.value(2).toLongLong(),
                                            .size = q.value(3).toLongLong()}};
    pkByPath_.insert(path, pk);
  }

  for (Folder &folder : folders_) {
    updateWatch(folder);
  }
}

MonitoredLibrary::AddFolderResult
MonitoredLibrary::addFolder(const QString &path) {
  const QFileInfo info(path);
  if (!info.exists() || !info.isDir()) {
    return {.error = tr("Not a folder: %1").arg(path)};
  }
  const QString canonical = info.canonicalFilePath();
  for (const Folder &folder : folders_) {
    if (isSameOrUnderPath(canonical, folder.path)) {
      if (isSameOrUnderPath(folder.path, canonical)) {
        return {.error = tr("Already watched: %1").arg(folder.path)};
      }
      return {.error = tr("Inside the watched folder %1").arg(folder.path)};
    }
    if (isSameOrUnderPath(folder.path, canonical)) {
      return {.error = tr("Contains the watched folder %1").arg(folder.path)};
    }
  }

  QSqlQuery q(databaseManager_.db());
  q.prepare("INSERT INTO library_folders(path) VALUES(:path)");
  q.bindValue(":path", canonical);
  if (!q.exec()) {
    qWarning() << "MonitoredLibrary::addFolder insert failed:"
               << q.lastError().text();
    return {.error =
                tr("Could not add the folder: %1").arg(q.lastError().text())};
  }
  const int folderId = q.lastInsertId().toInt();
  folders_.push_back(Folder{.id = folderId, .path = canonical});
  std::sort(folders_.begin(), folders_.end(),
            [](const Folder &a, const Folder &b) { return a.path < b.path; });
  updateWatch(*findFolder(folderId));
  requestFull(folderId);
  startNextScan();
  return {.folderId = folderId};
}

bool MonitoredLibrary::removeFolder(int folderId) {
  const auto folderIt =
      std::find_if(folders_.begin(), folders_.end(),
                   [folderId](const Folder &f) { return f.id == folderId; });
  if (folderIt == folders_.end()) {
    return false;
  }

  QSqlQuery q(databaseManager_.db());
  // monitored_files rows cascade.
  q.prepare("DELETE FROM library_folders WHERE folder_id=:folder_id");
  q.bindValue(":folder_id", folderId);
  execOrFatal(q, "MonitoredLibrary::removeFolder");

  const QString root = folderIt->path;
  folders_.erase(folderIt);
  pending_.erase(folderId);
  if (watched_.erase(folderId) > 0) {
    monitor_->removePath(root);
  }
  updateOfflinePoll();

  std::vector<int> left;
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->second.folderId == folderId) {
      left.push_back(it->first);
      pkByPath_.remove(it->second.path);
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = failed_.begin(); it != failed_.end();) {
    it = isSameOrUnderPath(it.key(), root) ? failed_.erase(it) : std::next(it);
  }
  if (!left.empty()) {
    emit songsLeft(left);
  }
  return true;
}

std::vector<MonitoredLibrary::Folder> MonitoredLibrary::folders() const {
  return folders_;
}

void MonitoredLibrary::rescan() {
  for (const Folder &folder : folders_) {
    requestFull(folder.id);
  }
  startNextScan();
}

void MonitoredLibrary::removeMissingSongs(int folderId) {
  if (findFolder(folderId)) {
    requestFull(folderId, true);
    startNextScan();
  }
}

void MonitoredLibrary::scanPaths(const QStringList &paths) {
  for (const QString &path : paths) {
    for (const Folder &folder : folders_) {
      if (isSameOrUnderPath(path, folder.path)) {
        requestPath(folder.id, path);
        break;
      }
    }
  }
  startNextScan();
}

bool MonitoredLibrary::isScanning() const {
  return scanWatcher_ != nullptr || applying_.has_value();
}

bool MonitoredLibrary::holds(int songPk) const {
  return entries_.contains(songPk);
}

std::vector<int> MonitoredLibrary::songPks() const {
  std::vector<int> pks;
  pks.reserve(entries_.size());
  for (const auto &[pk, entry] : entries_) {
    pks.push_back(pk);
  }
  return pks;
}

void MonitoredLibrary::noteSongWritten(int songPk) {
  const auto it = entries_.find(songPk);
  if (it == entries_.end()) {
    return;
  }
  const std::optional<FileStats> stats = readFileStats(it->second.path);
  if (!stats) {
    return;
  }
  it->second.stats = *stats;
  QSqlQuery q(databaseManager_.db());
  q.prepare(R"(
      UPDATE monitored_files SET mtime=:mtime, size=:size
      WHERE song_id=:song_id
  )");
  q.bindValue(":mtime", stats->mtimeNs);
  q.bindValue(":size", stats->size);
  q.bindValue(":song_id", songPk);
  execOrFatal(q, "MonitoredLibrary::noteSongWritten");
}

void MonitoredLibrary::checkOfflineFolders() {
  std::vector<int> available;
  for (const Folder &folder : folders_) {
    if (!folder.online &&
        isFolderRootAvailable(folder.path, hasFiles(folder.id))) {
      available.push_back(folder.id);
    }
  }
  // Looked up again: folderAvailabilityChanged handlers may change folders_.
  for (const int folderId : available) {
    if (Folder *folder = findFolder(folderId); folder && !folder->online) {
      setOnline(*folder, true);
      requestFull(folderId);
    }
  }
  startNextScan();
}

void MonitoredLibrary::requestFull(int folderId, bool skipAvailabilityCheck) {
  Request &request = pending_[folderId];
  request.full = true;
  request.paths.clear();
  request.skipAvailabilityCheck =
      request.skipAvailabilityCheck || skipAvailabilityCheck;
}

void MonitoredLibrary::requestPath(int folderId, const QString &path) {
  Request &request = pending_[folderId];
  if (!request.full && !request.paths.contains(path)) {
    request.paths.push_back(path);
  }
}

void MonitoredLibrary::startNextScan() {
  if (isScanning() || pending_.empty()) {
    return;
  }

  std::map<int, int> fileCounts;
  for (const auto &[pk, entry] : entries_) {
    ++fileCounts[entry.folderId];
  }

  LibraryScanInput input;
  input.registry = registry_;
  for (const auto &[folderId, request] : pending_) {
    const Folder *folder = findFolder(folderId);
    if (!folder) {
      continue;
    }
    ScanFolderInput folderInput{
        .folderId = folderId,
        .path = folder->path,
        .scopes = request.full ? QStringList{folder->path} : request.paths,
        .hasFiles = fileCounts[folderId] > 0,
        .skipAvailabilityCheck = request.skipAvailabilityCheck};
    for (const auto &[pk, entry] : entries_) {
      if (entry.folderId != folderId) {
        continue;
      }
      if (std::any_of(folderInput.scopes.begin(), folderInput.scopes.end(),
                      [&](const QString &scope) {
                        return isSameOrUnderPath(entry.path, scope);
                      })) {
        input.snapshot.insert(entry.path,
                              ScanSnapshotEntry{.pk = pk,
                                                .folderId = folderId,
                                                .stats = entry.stats});
      }
    }
    for (auto it = failed_.cbegin(); it != failed_.cend(); ++it) {
      if (std::any_of(folderInput.scopes.begin(), folderInput.scopes.end(),
                      [&](const QString &scope) {
                        return isSameOrUnderPath(it.key(), scope);
                      })) {
        input.failed.insert(it.key(), it.value());
      }
    }
    input.folders.push_back(std::move(folderInput));
  }
  pending_.clear();
  if (input.folders.empty()) {
    return;
  }

  scanWatcher_ = new QFutureWatcher<LibraryScanResult>(this);
  connect(scanWatcher_, &QFutureWatcher<LibraryScanResult>::finished, this,
          &MonitoredLibrary::onScanFinished);
  connect(scanWatcher_,
          &QFutureWatcher<LibraryScanResult>::progressValueChanged, this,
          [this, watcher = scanWatcher_](int value) {
            emit scanProgress(value, watcher->progressMaximum());
          });
  scanWatcher_->setFuture(
      QtConcurrent::run([input = std::move(input),
                         parse = parse_](QPromise<LibraryScanResult> &promise) {
        LibraryScanResult result =
            scanLibrary(input, parse, [&promise](int done, int total) {
              if (done == 0) {
                promise.setProgressRange(0, total);
              }
              promise.setProgressValue(done);
            });
        promise.addResult(std::move(result));
      }));
}

void MonitoredLibrary::onScanFinished() {
  LibraryScanResult result = scanWatcher_->result();
  scanWatcher_->deleteLater();
  scanWatcher_ = nullptr;

  for (const LibraryScanResult::Availability &availability :
       result.availability) {
    Folder *folder = findFolder(availability.folderId);
    if (folder && folder->online != availability.online) {
      setOnline(*folder, availability.online);
    }
  }
  for (const QString &path : result.clearedFailed) {
    failed_.remove(path);
  }
  for (auto it = result.newFailed.cbegin(); it != result.newFailed.cend();
       ++it) {
    if (std::any_of(folders_.begin(), folders_.end(), [&](const Folder &f) {
          return isSameOrUnderPath(it.key(), f.path);
        })) {
      failed_.insert(it.key(), it.value());
    }
  }

  applying_ = std::move(result);
  applyIndex_ = 0;
  applyNextBatch();
}

void MonitoredLibrary::applyNextBatch() {
  QSqlDatabase &db = databaseManager_.db();
  if (!db.transaction()) {
    qFatal("MonitoredLibrary::applyNextBatch: failed to start transaction: %s",
           db.lastError().text().toUtf8().data());
  }
  QSqlQuery insert(db);
  insert.prepare(R"(
      INSERT INTO monitored_files(song_id, folder_id, mtime, size)
      VALUES(:song_id, :folder_id, :mtime, :size)
  )");
  QSqlQuery update(db);
  update.prepare(R"(
      UPDATE monitored_files SET mtime=:mtime, size=:size
      WHERE song_id=:song_id
  )");
  QSqlQuery remove(db);
  remove.prepare("DELETE FROM monitored_files WHERE song_id=:song_id");

  std::vector<int> entered;
  std::vector<int> updated;
  std::vector<int> left;
  std::vector<std::pair<int, QString>> stale;
  std::vector<ScanEntry> &entries = applying_->entries;
  const size_t end = std::min(entries.size(), applyIndex_ + kApplyBatchSize);
  for (; applyIndex_ < end; ++applyIndex_) {
    ScanEntry &entry = entries[applyIndex_];
    if (!findFolder(entry.folderId)) {
      continue;
    }

    // Skip results older than the stored state (for example tags saved by
    // the app meanwhile), and scan the path again.
    if (entry.kind == ScanEntry::Kind::New) {
      if (pkByPath_.contains(entry.path)) {
        stale.emplace_back(entry.folderId, entry.path);
        continue;
      }
    } else {
      const auto stored = entries_.find(entry.pk);
      if (stored == entries_.end() || stored->second.path != entry.path ||
          stored->second.stats != entry.snapshotStats) {
        stale.emplace_back(entry.folderId, entry.path);
        continue;
      }
    }

    switch (entry.kind) {
    case ScanEntry::Kind::New: {
      entry.song.insert_or_assign(
          "filepath", FieldValue(entry.path.toStdString(), "filepath"));
      library_.prepareParsedSong(entry.song);
      const int pk = library_.addTolibrary(std::move(entry.song));
      if (entries_.contains(pk)) {
        // A second spelling of a monitored file; keep the first.
        qWarning() << "MonitoredLibrary: already monitored:" << entry.path;
        continue;
      }
      const QString path =
          QString::fromStdString(library_.getSongByPK(pk).at("filepath").text);
      insert.bindValue(":song_id", pk);
      insert.bindValue(":folder_id", entry.folderId);
      insert.bindValue(":mtime", entry.diskStats.mtimeNs);
      insert.bindValue(":size", entry.diskStats.size);
      execOrFatal(insert, "MonitoredLibrary: insert monitored_files");
      entries_[pk] = Entry{
          .folderId = entry.folderId, .path = path, .stats = entry.diskStats};
      pkByPath_.insert(path, pk);
      entered.push_back(pk);
      break;
    }
    case ScanEntry::Kind::Changed: {
      entry.song.insert_or_assign(
          "filepath", FieldValue(entry.path.toStdString(), "filepath"));
      library_.prepareParsedSong(entry.song);
      library_.addTolibrary(std::move(entry.song));
      update.bindValue(":mtime", entry.diskStats.mtimeNs);
      update.bindValue(":size", entry.diskStats.size);
      update.bindValue(":song_id", entry.pk);
      execOrFatal(update, "MonitoredLibrary: update monitored_files");
      entries_[entry.pk].stats = entry.diskStats;
      updated.push_back(entry.pk);
      break;
    }
    case ScanEntry::Kind::Gone:
      remove.bindValue(":song_id", entry.pk);
      execOrFatal(remove, "MonitoredLibrary: delete monitored_files");
      pkByPath_.remove(entry.path);
      entries_.erase(entry.pk);
      left.push_back(entry.pk);
      break;
    }
  }
  if (!db.commit()) {
    qFatal("MonitoredLibrary::applyNextBatch commit failed: %s",
           db.lastError().text().toUtf8().data());
  }

  // After the commit: songsLeft handlers delete song rows in their own
  // transaction.
  if (!entered.empty()) {
    emit songsEntered(entered);
  }
  if (!updated.empty()) {
    emit songsUpdated(updated);
  }
  if (!left.empty()) {
    emit songsLeft(left);
  }
  // The scan starts after this one finishes (finishApply).
  for (const auto &[folderId, path] : stale) {
    requestPath(folderId, path);
  }

  if (applying_ && applyIndex_ < applying_->entries.size()) {
    QTimer::singleShot(0, this, &MonitoredLibrary::applyNextBatch);
  } else {
    finishApply();
  }
}

void MonitoredLibrary::finishApply() {
  applying_.reset();
  applyIndex_ = 0;
  emit scanFinished();
  startNextScan();
}

void MonitoredLibrary::onPathsChanged(const QStringList &paths) {
  for (const QString &path : paths) {
    if (!changedPaths_.contains(path)) {
      changedPaths_.push_back(path);
    }
  }
  watchDebounceTimer_->start();
}

void MonitoredLibrary::onFolderNeedsRescan(const QString &path) {
  for (const Folder &folder : folders_) {
    if (isSameOrUnderPath(path, folder.path)) {
      requestFull(folder.id);
      startNextScan();
      return;
    }
  }
}

void MonitoredLibrary::setOnline(Folder &folder, bool online) {
  folder.online = online;
  updateWatch(folder);
  updateOfflinePoll();
  emit folderAvailabilityChanged(folder.id, online);
}

void MonitoredLibrary::updateWatch(Folder &folder) {
  if (!monitor_) {
    return;
  }
  const bool watched = watched_.contains(folder.id);
  if (folder.online && !watched) {
    if (monitor_->addPath(folder.path)) {
      watched_.insert(folder.id);
      folder.liveUpdatesUnavailable = false;
    } else {
      // A missing root is not reported: the next scan marks it offline.
      folder.liveUpdatesUnavailable = QFileInfo(folder.path).isDir();
    }
  } else if (!folder.online) {
    if (watched) {
      monitor_->removePath(folder.path);
      watched_.erase(folder.id);
    }
    folder.liveUpdatesUnavailable = false;
  }
}

void MonitoredLibrary::updateOfflinePoll() {
  const bool anyOffline =
      std::any_of(folders_.begin(), folders_.end(),
                  [](const Folder &folder) { return !folder.online; });
  if (!anyOffline) {
    offlinePollTimer_->stop();
  } else if (!offlinePollTimer_->isActive()) {
    offlinePollTimer_->start();
  }
}

bool MonitoredLibrary::hasFiles(int folderId) const {
  return std::any_of(entries_.begin(), entries_.end(), [&](const auto &item) {
    return item.second.folderId == folderId;
  });
}

const MonitoredLibrary::Folder *
MonitoredLibrary::findFolder(int folderId) const {
  const auto it =
      std::find_if(folders_.begin(), folders_.end(),
                   [folderId](const Folder &f) { return f.id == folderId; });
  return it == folders_.end() ? nullptr : &*it;
}

MonitoredLibrary::Folder *MonitoredLibrary::findFolder(int folderId) {
  return const_cast<Folder *>(std::as_const(*this).findFolder(folderId));
}
