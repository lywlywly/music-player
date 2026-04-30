#include "cloudplaystatssynccoordinator.h"
#include <QDateTime>
#include <QDebug>
#include <QSettings>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <memory>
#include <unordered_set>

namespace {
constexpr int kPullPageLimit = 100;
constexpr size_t kRebaseBulkChunkSize = 10;
constexpr int kRebaseBulkChunkGapMs = 1000;
} // namespace

CloudPlayStatsSyncCoordinator::CloudPlayStatsSyncCoordinator(
    SongLibrary &songLibrary, CloudPlayStatsSyncService &syncService,
    QObject *parent)
    : QObject(parent), songLibrary_(songLibrary), syncService_(syncService) {}

void CloudPlayStatsSyncCoordinator::startSync() {
  const QString uuid = currentValidUuid();
  if (uuid.isEmpty()) {
    qDebug() << "CloudSync start skipped: UUID not set/invalid";
    return;
  }

  QSettings settings;
  const bool rebasePending =
      settings.value("cloud_sync/rebase_pending", false).toBool();
  qDebug() << "CloudSync start:" << "rebasePending=" << rebasePending;
  if (rebasePending) {
    runRebaseSync(uuid);
    return;
  }
  runIncrementalSync(uuid);
}

void CloudPlayStatsSyncCoordinator::triggerManualRebase() {
  const QString uuid = currentValidUuid();
  if (uuid.isEmpty()) {
    qWarning() << "CloudSync manual rebase skipped: UUID not set/invalid";
    return;
  }
  qDebug() << "CloudSync manual rebase triggered";
  runRebaseSync(uuid);
}

void CloudPlayStatsSyncCoordinator::pushIncrementForSongPk(int songPk) {
  const QString uuid = currentValidUuid();
  if (uuid.isEmpty()) {
    return;
  }
  const std::string identityKey = songLibrary_.songIdentityKeyBySongPk(songPk);
  if (identityKey.empty()) {
    return;
  }
  syncService_.pushIncrement(uuid, identityKey, 1);
}

bool CloudPlayStatsSyncCoordinator::hasValidUuid() const {
  return !currentValidUuid().isEmpty();
}

void CloudPlayStatsSyncCoordinator::runIncrementalSync(const QString &uuid) {
  QSettings settings;
  const qint64 lastSyncedAt =
      settings.value("cloud_sync/last_synced_at", 0).toLongLong();
  const qint64 updatedAfter = std::max<qint64>(0, lastSyncedAt - 60);
  qDebug() << "CloudSync incremental begin:" << "lastSyncedAt=" << lastSyncedAt
           << "updatedAfter=" << updatedAfter;
  syncService_.pullDeltaPaged(
      uuid, updatedAfter, kPullPageLimit,
      [this](const std::vector<CloudPlayStatItem> &items) {
        applyCloudPullPage(items, nullptr,
                           "CloudSync incremental page applied");
      },
      [](bool ok, qint64 maxUpdatedAt) {
        if (!ok) {
          qWarning() << "CloudSync incremental failed";
          return;
        }
        QSettings settings;
        const qint64 nextCursor = std::max(maxUpdatedAt, unixNowSeconds());
        settings.setValue("cloud_sync/last_synced_at", nextCursor);
        qDebug() << "CloudSync incremental done:" << "maxUpdatedAt="
                 << maxUpdatedAt << "nextCursor=" << nextCursor;
      });
}

void CloudPlayStatsSyncCoordinator::runRebaseSync(const QString &uuid) {
  qDebug() << "CloudSync rebase begin";
  auto cloudCounts = std::make_shared<std::unordered_map<std::string, int>>();
  syncService_.pullDeltaPaged(
      uuid, 0, kPullPageLimit,
      [this, cloudCounts](const std::vector<CloudPlayStatItem> &items) {
        applyCloudPullPage(items, cloudCounts.get(),
                           "CloudSync rebase pull page applied");
      },
      [this, uuid, cloudCounts](bool ok, qint64) {
        if (!ok) {
          qWarning()
              << "CloudSync rebase pull failed; keep rebase_pending=true";
          return;
        }

        std::vector<std::pair<std::string, int>> deltas =
            buildRebaseDeltas(*cloudCounts);
        if (deltas.empty()) {
          finishRebaseSuccess();
          qDebug() << "CloudSync rebase done: no local deltas";
          return;
        }

        qDebug() << "CloudSync rebase push deltas:" << deltas.size();
        pushRebaseDeltasSequentially(uuid, std::move(deltas));
      });
}

void CloudPlayStatsSyncCoordinator::applyCloudPullPage(
    const std::vector<CloudPlayStatItem> &items,
    std::unordered_map<std::string, int> *cloudCounts, const char *logTag) {
  std::unordered_set<int> affected;
  for (const CloudPlayStatItem &item : items) {
    if (cloudCounts) {
      (*cloudCounts)[item.songIdentityKey] = item.playCount;
    }
    const std::vector<int> pks = songLibrary_.applyCloudPlayCount(
        item.songIdentityKey, item.playCount, item.updatedAt);
    affected.insert(pks.begin(), pks.end());
  }
  for (int pk : affected) {
    emit songDataChanged(pk);
  }
  qDebug() << logTag << "items=" << items.size()
           << "affectedSongs=" << affected.size();
}

std::vector<std::pair<std::string, int>>
CloudPlayStatsSyncCoordinator::buildRebaseDeltas(
    const std::unordered_map<std::string, int> &cloudCounts) const {
  std::vector<std::pair<std::string, int>> deltas;
  const auto localCounts = songLibrary_.identityPlayCounts();
  deltas.reserve(localCounts.size());
  for (const auto &[identityKey, localCount] : localCounts) {
    const auto cloudIt = cloudCounts.find(identityKey);
    const int cloudCount = (cloudIt == cloudCounts.end()) ? 0 : cloudIt->second;
    const int delta = localCount - cloudCount;
    if (delta > 0) {
      deltas.push_back({identityKey, delta});
    }
  }
  return deltas;
}

void CloudPlayStatsSyncCoordinator::finishRebaseSuccess() {
  QSettings settings;
  settings.setValue("cloud_sync/rebase_pending", false);
  settings.setValue("cloud_sync/last_synced_at", unixNowSeconds());
}

void CloudPlayStatsSyncCoordinator::pushRebaseDeltasSequentially(
    const QString &uuid, std::vector<std::pair<std::string, int>> deltas) {
  auto sharedDeltas =
      std::make_shared<std::vector<std::pair<std::string, int>>>(
          std::move(deltas));
  auto index = std::make_shared<int>(0);
  auto allOk = std::make_shared<bool>(true);
  auto pushNext = std::make_shared<std::function<void()>>();
  *pushNext = [this, uuid, sharedDeltas, index, allOk, pushNext]() {
    if (*index >= static_cast<int>(sharedDeltas->size())) {
      if (*allOk) {
        finishRebaseSuccess();
        qDebug() << "CloudSync rebase done: all delta pushes succeeded";
      } else {
        qWarning() << "CloudSync rebase incomplete: some delta pushes failed";
      }
      return;
    }

    const int begin = *index;
    const int end = std::min(begin + static_cast<int>(kRebaseBulkChunkSize),
                             static_cast<int>(sharedDeltas->size()));
    std::vector<std::pair<std::string, int>> chunk;
    chunk.reserve(static_cast<size_t>(end - begin));
    for (int i = begin; i < end; ++i) {
      chunk.push_back(sharedDeltas->at(i));
    }
    qDebug() << "CloudSync rebase bulk push:" << "begin=" << begin
             << "end=" << end << "count=" << chunk.size();

    syncService_.pushBulkIncrement(
        uuid, chunk, [this, index, end, allOk, pushNext](bool ok) {
          if (!ok) {
            *allOk = false;
            qWarning() << "CloudSync rebase bulk push failed at index"
                       << *index;
            return;
          }
          *index = end;
          QTimer::singleShot(kRebaseBulkChunkGapMs, this,
                             [pushNext]() { (*pushNext)(); });
        });
  };
  (*pushNext)();
}

bool CloudPlayStatsSyncCoordinator::isValidUuid(const QString &uuid) {
  return !uuid.trimmed().isEmpty() && !QUuid(uuid.trimmed()).isNull();
}

qint64 CloudPlayStatsSyncCoordinator::unixNowSeconds() {
  return QDateTime::currentSecsSinceEpoch();
}

QString CloudPlayStatsSyncCoordinator::currentUuid() const {
  QSettings settings;
  return settings.value("cloud_sync/user_uuid").toString().trimmed();
}

QString CloudPlayStatsSyncCoordinator::currentValidUuid() const {
  const QString uuid = currentUuid();
  return isValidUuid(uuid) ? uuid : QString{};
}
