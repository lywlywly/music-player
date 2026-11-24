#ifndef CLOUDPLAYSTATSSYNCCOORDINATOR_H
#define CLOUDPLAYSTATSSYNCCOORDINATOR_H

#include "cloudplaystatssyncservice.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

class PlayStatsStore;

// CloudPlayStatsSyncCoordinator owns cloud-sync workflow policy on top of the
// HTTP client and the play stats file. Lives on the main thread; the requests
// run on its worker thread.
class CloudPlayStatsSyncCoordinator : public QObject {
  Q_OBJECT

public:
  // onValuesChanged gets the keys of each cloud page merged into store.
  CloudPlayStatsSyncCoordinator(
      PlayStatsStore &store,
      std::function<void(const QStringList &keys)> onValuesChanged,
      QObject *parent = nullptr);
  // Stops the worker thread: waits for a request it runs to end.
  ~CloudPlayStatsSyncCoordinator() override;

  // Starts sync from persisted state: rebase if `rebase_pending`, otherwise
  // incremental pull.
  void startSync();

  // Triggers pull-first rebase immediately for current UUID.
  void triggerManualRebase();

  // Best-effort cloud push for a completed local play-count increment.
  void pushIncrement(const QString &songIdentityKey);

  // Returns whether current persisted UUID is valid for cloud sync.
  bool hasValidUuid() const;

private:
  void runIncrementalSync(const QString &uuid);
  void runRebaseSync(const QString &uuid);
  void pushIncrementOnWorker(const QString &uuid,
                             const std::string &songIdentityKey, int delta);
  void pushBulkIncrementOnWorker(
      const QString &uuid,
      const std::vector<std::pair<std::string, int>> &updates,
      const std::function<void(bool ok)> &onFinished);
  void pullDeltaPagedOnWorker(
      const QString &uuid, qint64 updatedAfter, int pageLimit,
      const std::function<void(const std::vector<CloudPlayStatItem> &)> &onPage,
      const std::function<void(bool ok, qint64 maxUpdatedAt)> &onFinished);
  void applyCloudPullPage(const std::vector<CloudPlayStatItem> &items,
                          std::unordered_map<std::string, int> *cloudCounts,
                          const char *logTag);
  std::vector<std::pair<std::string, int>> buildRebaseDeltas(
      const std::unordered_map<std::string, int> &cloudCounts) const;
  void finishRebaseSuccess();
  // Pushes the chunk of deltas starting at begin, then the next one after a
  // gap; stops at a failure, leaving rebase_pending set.
  void pushRebaseChunk(
      const QString &uuid,
      std::shared_ptr<const std::vector<std::pair<std::string, int>>> deltas,
      size_t begin);
  static qint64 unixNowSeconds();
  QString currentUuid() const;
  QString currentValidUuid() const;

  PlayStatsStore &store_;
  std::function<void(const QStringList &keys)> onValuesChanged_;
  QThread syncThread_;
  QObject syncWorker_;
};

#endif // CLOUDPLAYSTATSSYNCCOORDINATOR_H
