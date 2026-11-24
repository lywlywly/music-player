#include "cloudplaystatssynccoordinator.h"
#include "cloudsyncsettings.h"
#include "playstatsstore.h"
#include <QDateTime>
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <algorithm>
#include <memory>

namespace {
constexpr int kPullPageLimit = 100;
constexpr size_t kRebaseBulkChunkSize = 10;
constexpr int kRebaseBulkChunkGapMs = 1000;
} // namespace

CloudPlayStatsSyncCoordinator::CloudPlayStatsSyncCoordinator(
    PlayStatsStore &store,
    std::function<void(const QStringList &keys)> onValuesChanged,
    QObject *parent)
    : QObject(parent), store_(store),
      onValuesChanged_(std::move(onValuesChanged)) {
  syncWorker_.moveToThread(&syncThread_);
  syncThread_.start();
}

CloudPlayStatsSyncCoordinator::~CloudPlayStatsSyncCoordinator() {
  if (syncThread_.isRunning()) {
    QThread *ownerThread = thread();
    QMetaObject::invokeMethod(
        &syncWorker_,
        [this, ownerThread]() { syncWorker_.moveToThread(ownerThread); },
        Qt::BlockingQueuedConnection);
    syncThread_.quit();
    syncThread_.wait();
  }
}

void CloudPlayStatsSyncCoordinator::startSync() {
  const QString uuid = currentValidUuid();
  if (uuid.isEmpty()) {
    qDebug() << "CloudSync start skipped: UUID not set/invalid";
    return;
  }

  QSettings settings;
  const bool rebasePending =
      settings.value(CloudSyncSettings::kRebasePending, false).toBool();
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

void CloudPlayStatsSyncCoordinator::pushIncrement(
    const QString &songIdentityKey) {
  const QString uuid = currentValidUuid();
  if (uuid.isEmpty()) {
    return;
  }
  pushIncrementOnWorker(uuid, songIdentityKey.toStdString(), 1);
}

bool CloudPlayStatsSyncCoordinator::hasValidUuid() const {
  return !currentValidUuid().isEmpty();
}

void CloudPlayStatsSyncCoordinator::runIncrementalSync(const QString &uuid) {
  QSettings settings;
  const qint64 lastSyncedAt =
      settings.value(CloudSyncSettings::kLastSyncedAt, 0).toLongLong();
  const qint64 updatedAfter = std::max<qint64>(0, lastSyncedAt - 60);
  qDebug() << "CloudSync incremental begin:" << "lastSyncedAt=" << lastSyncedAt
           << "updatedAfter=" << updatedAfter;
  pullDeltaPagedOnWorker(
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
        settings.setValue(CloudSyncSettings::kLastSyncedAt, nextCursor);
        qDebug() << "CloudSync incremental done:" << "maxUpdatedAt="
                 << maxUpdatedAt << "nextCursor=" << nextCursor;
      });
}

void CloudPlayStatsSyncCoordinator::runRebaseSync(const QString &uuid) {
  qDebug() << "CloudSync rebase begin";
  auto cloudCounts = std::make_shared<std::unordered_map<std::string, int>>();
  pullDeltaPagedOnWorker(
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
        pushRebaseChunk(
            uuid,
            std::make_shared<const std::vector<std::pair<std::string, int>>>(
                std::move(deltas)),
            0);
      });
}

void CloudPlayStatsSyncCoordinator::applyCloudPullPage(
    const std::vector<CloudPlayStatItem> &items,
    std::unordered_map<std::string, int> *cloudCounts, const char *logTag) {
  std::vector<std::pair<QString, int>> counts;
  QStringList keys;
  counts.reserve(items.size());
  for (const CloudPlayStatItem &item : items) {
    if (cloudCounts) {
      (*cloudCounts)[item.songIdentityKey] = item.playCount;
    }
    if (item.songIdentityKey.empty() || item.playCount < 0) {
      continue;
    }
    const QString key = QString::fromStdString(item.songIdentityKey);
    counts.emplace_back(key, item.playCount);
    keys.push_back(key);
  }
  store_.mergeCloudCounts(counts);
  onValuesChanged_(keys);
  qDebug() << logTag << "items=" << items.size();
}

std::vector<std::pair<std::string, int>>
CloudPlayStatsSyncCoordinator::buildRebaseDeltas(
    const std::unordered_map<std::string, int> &cloudCounts) const {
  std::vector<std::pair<std::string, int>> deltas;
  const auto localCounts = store_.playCounts();
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
  settings.setValue(CloudSyncSettings::kRebasePending, false);
  settings.setValue(CloudSyncSettings::kLastSyncedAt, unixNowSeconds());
}

void CloudPlayStatsSyncCoordinator::pushRebaseChunk(
    const QString &uuid,
    std::shared_ptr<const std::vector<std::pair<std::string, int>>> deltas,
    size_t begin) {
  const size_t end = std::min(begin + kRebaseBulkChunkSize, deltas->size());
  const std::vector<std::pair<std::string, int>> chunk(
      deltas->begin() + static_cast<std::ptrdiff_t>(begin),
      deltas->begin() + static_cast<std::ptrdiff_t>(end));
  qDebug() << "CloudSync rebase bulk push:" << "begin=" << begin
           << "end=" << end;
  // Only the pending callback or timer holds deltas, so they are freed after
  // the last chunk, a failure, or the coordinator's destruction (which drops
  // the queued callback and the timer).
  pushBulkIncrementOnWorker(
      uuid, chunk, [this, uuid, deltas, begin, end](bool ok) {
        if (!ok) {
          qWarning() << "CloudSync rebase bulk push failed at index" << begin
                     << "; keep rebase_pending=true";
          return;
        }
        if (end == deltas->size()) {
          finishRebaseSuccess();
          qDebug() << "CloudSync rebase done: all delta pushes succeeded";
          return;
        }
        QTimer::singleShot(kRebaseBulkChunkGapMs, this,
                           [this, uuid, deltas, end]() {
                             pushRebaseChunk(uuid, deltas, end);
                           });
      });
}

void CloudPlayStatsSyncCoordinator::pushIncrementOnWorker(
    const QString &uuid, const std::string &songIdentityKey, int delta) {
  QMetaObject::invokeMethod(
      &syncWorker_,
      [uuid, songIdentityKey, delta]() {
        CloudPlayStatsSync::pushIncrement(uuid, songIdentityKey, delta);
      },
      Qt::QueuedConnection);
}

void CloudPlayStatsSyncCoordinator::pushBulkIncrementOnWorker(
    const QString &uuid,
    const std::vector<std::pair<std::string, int>> &updates,
    const std::function<void(bool ok)> &onFinished) {
  QPointer<CloudPlayStatsSyncCoordinator> guard(this);
  QMetaObject::invokeMethod(
      &syncWorker_,
      [uuid, updates, onFinished, guard]() {
        const bool ok = CloudPlayStatsSync::pushBulkIncrement(uuid, updates);
        if (!guard) {
          return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [onFinished, ok]() {
              if (onFinished) {
                onFinished(ok);
              }
            },
            Qt::QueuedConnection);
      },
      Qt::QueuedConnection);
}

void CloudPlayStatsSyncCoordinator::pullDeltaPagedOnWorker(
    const QString &uuid, qint64 updatedAfter, int pageLimit,
    const std::function<void(const std::vector<CloudPlayStatItem> &)> &onPage,
    const std::function<void(bool ok, qint64 maxUpdatedAt)> &onFinished) {
  QPointer<CloudPlayStatsSyncCoordinator> guard(this);
  QMetaObject::invokeMethod(
      &syncWorker_,
      [uuid, updatedAfter, pageLimit, onPage, onFinished, guard]() {
        CloudPlayStatsPullResult result =
            CloudPlayStatsSync::pullDeltaPaged(uuid, updatedAfter, pageLimit);
        if (!guard) {
          return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [onPage, onFinished, result = std::move(result)]() {
              if (onPage) {
                for (const auto &page : result.pages) {
                  onPage(page);
                }
              }
              if (onFinished) {
                onFinished(result.ok, result.maxUpdatedAt);
              }
            },
            Qt::QueuedConnection);
      },
      Qt::QueuedConnection);
}

qint64 CloudPlayStatsSyncCoordinator::unixNowSeconds() {
  return QDateTime::currentSecsSinceEpoch();
}

QString CloudPlayStatsSyncCoordinator::currentUuid() const {
  QSettings settings;
  return settings.value(CloudSyncSettings::kUserUuid).toString().trimmed();
}

QString CloudPlayStatsSyncCoordinator::currentValidUuid() const {
  const QString uuid = currentUuid();
  return CloudSyncSettings::isValidUuid(uuid) ? uuid : QString{};
}
