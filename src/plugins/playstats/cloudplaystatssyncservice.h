#ifndef CLOUDPLAYSTATSSYNCSERVICE_H
#define CLOUDPLAYSTATSSYNCSERVICE_H

#include <QString>
#include <vector>

struct CloudPlayStatItem {
  std::string songIdentityKey;
  int playCount = 0;
  qint64 updatedAt = 0;
};

struct CloudPlayStatsPullResult {
  bool ok = false;
  qint64 maxUpdatedAt = 0;
  std::vector<std::vector<CloudPlayStatItem>> pages;
};

namespace CloudPlayStatsSync {

// Blocking HTTP functions for the AWS Lambda endpoint. Call from a worker
// thread, not from the UI thread.
bool pushIncrement(const QString &userUuid, const std::string &songIdentityKey,
                   int delta = 1);

bool pushBulkIncrement(const QString &userUuid,
                       const std::vector<std::pair<std::string, int>> &updates);

CloudPlayStatsPullResult pullDeltaPaged(const QString &userUuid,
                                        qint64 updatedAfter, int pageLimit);

#ifdef MYPLAYER_TESTING
struct DummyPushCall {
  QString userUuid;
  std::string songIdentityKey;
  int delta = 0;
};
struct TestingHttpStep {
  bool ok = false;
  bool throttled = false;
  bool timedOut = false;
};
struct TestingRetryResult {
  bool ok = false;
  int attempts = 0;
};

void setDummyPullPages(const std::vector<std::vector<CloudPlayStatItem>> &pages,
                       bool ok = true, qint64 maxUpdatedAt = 0);
void clearDummyPullPages();
std::vector<DummyPushCall> dummyPushCalls();
void clearDummyPushCalls();
void setDummyPushResults(const std::vector<bool> &results);
TestingRetryResult
testingRunRetrySequence(const std::vector<TestingHttpStep> &steps);
#endif

} // namespace CloudPlayStatsSync

#endif // CLOUDPLAYSTATSSYNCSERVICE_H
