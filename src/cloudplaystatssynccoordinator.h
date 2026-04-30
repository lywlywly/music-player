#ifndef CLOUDPLAYSTATSSYNCCOORDINATOR_H
#define CLOUDPLAYSTATSSYNCCOORDINATOR_H

#include "cloudplaystatssyncservice.h"
#include "songlibrary.h"
#include <QObject>
#include <QString>
#include <unordered_map>
#include <vector>

// CloudPlayStatsSyncCoordinator owns cloud-sync workflow policy on top of the
// HTTP client and SongLibrary.
class CloudPlayStatsSyncCoordinator : public QObject {
  Q_OBJECT

public:
  CloudPlayStatsSyncCoordinator(SongLibrary &songLibrary,
                                CloudPlayStatsSyncService &syncService,
                                QObject *parent = nullptr);

  // Starts sync from persisted state: rebase if `rebase_pending`, otherwise
  // incremental pull.
  void startSync();

  // Triggers pull-first rebase immediately for current UUID.
  void triggerManualRebase();

  // Best-effort cloud push for a completed local play-count increment.
  void pushIncrementForSongPk(int songPk);

  // Returns whether current persisted UUID is valid for cloud sync.
  bool hasValidUuid() const;

signals:
  // Emitted for each song PK whose local fields changed due to cloud merge.
  void songDataChanged(int songPk);

private:
  void runIncrementalSync(const QString &uuid);
  void runRebaseSync(const QString &uuid);
  void applyCloudPullPage(
      const std::vector<CloudPlayStatItem> &items,
      std::unordered_map<std::string, int> *cloudCounts = nullptr,
      const char *logTag = "CloudSync pull page applied");
  std::vector<std::pair<std::string, int>> buildRebaseDeltas(
      const std::unordered_map<std::string, int> &cloudCounts) const;
  void finishRebaseSuccess();
  void
  pushRebaseDeltasSequentially(const QString &uuid,
                               std::vector<std::pair<std::string, int>> deltas);
  static bool isValidUuid(const QString &uuid);
  static qint64 unixNowSeconds();
  QString currentUuid() const;
  QString currentValidUuid() const;

  SongLibrary &songLibrary_;
  CloudPlayStatsSyncService &syncService_;
};

#endif // CLOUDPLAYSTATSSYNCCOORDINATOR_H
