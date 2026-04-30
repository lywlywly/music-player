#ifndef CLOUDPLAYSTATSSYNCSERVICE_H
#define CLOUDPLAYSTATSSYNCSERVICE_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <functional>
#include <vector>

struct CloudPlayStatItem {
  std::string songIdentityKey;
  int playCount = 0;
  qint64 updatedAt = 0;
};

// CloudPlayStatsSyncService is a thin HTTP client for the AWS Lambda endpoint.
// It performs asynchronous requests, retries on throttle with a fixed delay,
// and streams pull pages through callbacks.
class CloudPlayStatsSyncService : public QObject {
  Q_OBJECT
public:
  explicit CloudPlayStatsSyncService(QObject *parent = nullptr);

  // Sends one increment update:
  // { user_uuid, song_identity_key, delta }.
  // Optional callback reports final success/failure.
  void pushIncrement(const QString &userUuid,
                     const std::string &songIdentityKey, int delta = 1);
  void pushIncrement(const QString &userUuid,
                     const std::string &songIdentityKey, int delta,
                     const std::function<void(bool ok)> &onFinished);

  // Sends a bulk increment update:
  // { user_uuid, updates: [{ song_identity_key, delta }, ...] }.
  // Caller controls chunk sizing.
  void
  pushBulkIncrement(const QString &userUuid,
                    const std::vector<std::pair<std::string, int>> &updates,
                    const std::function<void(bool ok)> &onFinished);

  // Pulls cloud stats with paging. `updatedAfter` is the lower cursor bound.
  // `onPage` is called for each page. `onFinished(ok, maxUpdatedAt)` is called
  // once when the pull completes or fails.
  void pullDeltaPaged(
      const QString &userUuid, qint64 updatedAfter, int pageLimit,
      const std::function<void(const std::vector<CloudPlayStatItem> &)> &onPage,
      const std::function<void(bool ok, qint64 maxUpdatedAt)> &onFinished);

#ifdef MYPLAYER_TESTING
  struct DummyPushCall {
    QString userUuid;
    std::string songIdentityKey;
    int delta = 0;
  };

  static void
  setDummyPullPages(const std::vector<std::vector<CloudPlayStatItem>> &pages,
                    bool ok = true, qint64 maxUpdatedAt = 0);
  static void clearDummyPullPages();
  static std::vector<DummyPushCall> dummyPushCalls();
  static void clearDummyPushCalls();
  static void setDummyPushResults(const std::vector<bool> &results);
#endif

private:
  QNetworkAccessManager network_;
};

#endif // CLOUDPLAYSTATSSYNCSERVICE_H
