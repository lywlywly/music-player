#include "cloudplaystatssyncservice.h"
#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

namespace {
constexpr auto kCloudPlayStatsUrl =
    "https://y2khecz4nrzrqjy4t556erix2e0hcqrm.lambda-url.us-east-2.on.aws/";
constexpr int kRetryDelayMs = 60000;
constexpr int kRequestTimeoutMs = 15000;
constexpr int kMaxRequestRetries = 3;
constexpr int kPullPageGapMs = 1000;

struct HttpResult {
  bool ok = false;
  bool throttled = false;
  bool timedOut = false;
  QByteArray body;
  QString errorString;
};

#ifdef MYPLAYER_TESTING
struct DummyCloudSyncState {
  std::vector<std::vector<CloudPlayStatItem>> pullPages;
  bool pullOk = true;
  qint64 pullMaxUpdatedAt = 0;
  std::vector<CloudPlayStatsSync::DummyPushCall> pushCalls;
  std::vector<bool> pushResults;
};

DummyCloudSyncState &dummyState() {
  static DummyCloudSyncState state;
  return state;
}

bool useDummyCloudSync() {
  return qEnvironmentVariableIntValue("MYPLAYER_USE_DUMMY_CLOUD_SYNC") != 0;
}

int testingIntOverride(const char *name, int fallback) {
  bool ok = false;
  const int value = qEnvironmentVariableIntValue(name, &ok);
  return ok ? value : fallback;
}

bool nextDummyPushResult() {
  bool ok = true;
  if (!dummyState().pushResults.empty()) {
    ok = dummyState().pushResults.front();
    dummyState().pushResults.erase(dummyState().pushResults.begin());
  }
  return ok;
}
#endif

bool isThrottleResponse(QNetworkReply *reply, const QByteArray &body) {
  const int status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (status == 429) {
    return true;
  }

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    return false;
  }
  const QString errorCode = doc.object().value("error_code").toString();
  return errorCode == "ProvisionedThroughputExceededException" ||
         errorCode == "ThrottlingException" ||
         errorCode == "RequestLimitExceeded";
}

void waitForDelay(int delayMs) {
  QEventLoop loop;
  QTimer::singleShot(delayMs, &loop, &QEventLoop::quit);
  loop.exec();
}

int retryDelayMs() {
#ifdef MYPLAYER_TESTING
  return testingIntOverride("MYPLAYER_CLOUD_SYNC_RETRY_DELAY_MS",
                            kRetryDelayMs);
#else
  return kRetryDelayMs;
#endif
}

QNetworkRequest makeRequest(const QUrl &url) {
  QNetworkRequest request(url);
  request.setTransferTimeout(kRequestTimeoutMs);
  return request;
}

HttpResult waitForReply(QNetworkReply *reply) {
  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();

  HttpResult result;
  result.body = reply->readAll();
  result.throttled = isThrottleResponse(reply, result.body);
  result.timedOut = reply->error() == QNetworkReply::TimeoutError;
  result.ok = !result.throttled && reply->error() == QNetworkReply::NoError;
  result.errorString = reply->errorString();
  delete reply;
  return result;
}

bool retryRequestIfNeeded(const HttpResult &result, const char *logTag,
                          int &retries) {
  if (!result.throttled && !result.timedOut) {
    return false;
  }
  if (retries >= kMaxRequestRetries) {
    qWarning() << logTag << "retry limit reached";
    return false;
  }
  ++retries;
  qWarning() << logTag << (result.timedOut ? "timed out;" : "throttled;")
             << "retry" << retries << "of" << kMaxRequestRetries << "in"
             << retryDelayMs() << "ms";
  waitForDelay(retryDelayMs());
  return true;
}

template <typename StartRequest>
HttpResult sendWithRetries(StartRequest startRequest, const char *logTag) {
  int retries = 0;
  while (true) {
    HttpResult result = waitForReply(startRequest());
    if (!retryRequestIfNeeded(result, logTag, retries)) {
      return result;
    }
  }
}
} // namespace

namespace CloudPlayStatsSync {

#ifdef MYPLAYER_TESTING
void setDummyPullPages(const std::vector<std::vector<CloudPlayStatItem>> &pages,
                       bool ok, qint64 maxUpdatedAt) {
  dummyState().pullPages = pages;
  dummyState().pullOk = ok;
  dummyState().pullMaxUpdatedAt = maxUpdatedAt;
}

void clearDummyPullPages() {
  dummyState().pullPages.clear();
  dummyState().pullOk = true;
  dummyState().pullMaxUpdatedAt = 0;
}

std::vector<DummyPushCall> dummyPushCalls() { return dummyState().pushCalls; }

void clearDummyPushCalls() {
  dummyState().pushCalls.clear();
  dummyState().pushResults.clear();
}

void setDummyPushResults(const std::vector<bool> &results) {
  dummyState().pushResults = results;
}

TestingRetryResult
testingRunRetrySequence(const std::vector<TestingHttpStep> &steps) {
  int retries = 0;
  TestingRetryResult result;
  for (const TestingHttpStep &step : steps) {
    ++result.attempts;
    HttpResult httpResult;
    httpResult.ok = step.ok;
    httpResult.throttled = step.throttled;
    httpResult.timedOut = step.timedOut;
    if (!retryRequestIfNeeded(httpResult, "CloudSync test", retries)) {
      result.ok = httpResult.ok;
      return result;
    }
  }
  return result;
}
#endif

bool pushIncrement(const QString &userUuid, const std::string &songIdentityKey,
                   int delta) {
#ifdef MYPLAYER_TESTING
  if (useDummyCloudSync()) {
    dummyState().pushCalls.push_back({userUuid, songIdentityKey, delta});
    return nextDummyPushResult();
  }
#endif

  qDebug() << "CloudSync push begin:" << "identityKey="
           << QString::fromStdString(songIdentityKey) << "delta=" << delta;

  QNetworkRequest request =
      makeRequest(QUrl(QString::fromUtf8(kCloudPlayStatsUrl)));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  QJsonObject body{
      {"user_uuid", userUuid},
      {"song_identity_key", QString::fromStdString(songIdentityKey)},
      {"delta", delta},
  };

  QNetworkAccessManager network;
  const QByteArray payload = QJsonDocument(body).toJson();
  const HttpResult result = sendWithRetries(
      [&]() { return network.post(request, payload); }, "cloud push");
  if (!result.ok) {
    qWarning() << "cloud push failed:" << result.errorString;
    return false;
  }
  qDebug() << "CloudSync push success";
  return true;
}

bool pushBulkIncrement(
    const QString &userUuid,
    const std::vector<std::pair<std::string, int>> &updates) {
#ifdef MYPLAYER_TESTING
  if (useDummyCloudSync()) {
    for (const auto &[songIdentityKey, delta] : updates) {
      dummyState().pushCalls.push_back({userUuid, songIdentityKey, delta});
    }
    return nextDummyPushResult();
  }
#endif

  QJsonArray updatesArray;
  for (const auto &[songIdentityKey, delta] : updates) {
    updatesArray.push_back(QJsonObject{
        {"song_identity_key", QString::fromStdString(songIdentityKey)},
        {"delta", delta},
    });
  }
  qDebug() << "CloudSync bulk push begin: updates=" << updates.size();

  QNetworkRequest request =
      makeRequest(QUrl(QString::fromUtf8(kCloudPlayStatsUrl)));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  QJsonObject body{
      {"user_uuid", userUuid},
      {"updates", updatesArray},
  };

  QNetworkAccessManager network;
  const QByteArray payload = QJsonDocument(body).toJson();
  const HttpResult result = sendWithRetries(
      [&]() { return network.post(request, payload); }, "cloud bulk push");
  if (!result.ok) {
    qWarning() << "cloud bulk push failed:" << result.errorString;
    return false;
  }
  qDebug() << "CloudSync bulk push success";
  return true;
}

CloudPlayStatsPullResult pullDeltaPaged(const QString &userUuid,
                                        qint64 updatedAfter, int pageLimit) {
  CloudPlayStatsPullResult result;

#ifdef MYPLAYER_TESTING
  if (useDummyCloudSync()) {
    result.ok = dummyState().pullOk;
    result.pages = dummyState().pullPages;
    result.maxUpdatedAt = dummyState().pullMaxUpdatedAt;
    if (result.maxUpdatedAt == 0) {
      for (const auto &page : result.pages) {
        for (const CloudPlayStatItem &item : page) {
          result.maxUpdatedAt = std::max(result.maxUpdatedAt, item.updatedAt);
        }
      }
    }
    return result;
  }
#endif

  qDebug() << "CloudSync pull begin:" << "updatedAfter=" << updatedAfter
           << "pageLimit=" << pageLimit;

  int pageCount = 0;
  QString cursor;
  QNetworkAccessManager network;
  while (true) {
    QUrl url(QString::fromUtf8(kCloudPlayStatsUrl));
    QUrlQuery query;
    query.addQueryItem("user_uuid", userUuid);
    query.addQueryItem("updated_after", QString::number(updatedAfter));
    query.addQueryItem("limit", QString::number(pageLimit));
    if (!cursor.isEmpty()) {
      query.addQueryItem("last_evaluated_key", cursor);
    }
    url.setQuery(query);

    QNetworkRequest request = makeRequest(url);
    const HttpResult httpResult =
        sendWithRetries([&]() { return network.get(request); }, "cloud pull");
    if (!httpResult.ok) {
      qWarning() << "cloud pull failed:" << httpResult.errorString;
      return result;
    }

    QJsonParseError parseError;
    const QJsonDocument doc =
        QJsonDocument::fromJson(httpResult.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
      qWarning() << "cloud pull json parse error:" << parseError.errorString();
      return result;
    }

    const QJsonObject root = doc.object();
    if (!root.value("ok").toBool()) {
      qWarning() << "cloud pull api error:" << root;
      return result;
    }

    std::vector<CloudPlayStatItem> pageItems;
    const QJsonArray items = root.value("items").toArray();
    pageItems.reserve(items.size());
    for (const QJsonValue &value : items) {
      const QJsonObject itemObj = value.toObject();
      CloudPlayStatItem item;
      item.songIdentityKey =
          itemObj.value("song_identity_key").toString().toStdString();
      item.playCount = itemObj.value("play_count").toInt();
      item.updatedAt =
          static_cast<qint64>(itemObj.value("updated_at").toDouble());
      result.maxUpdatedAt = std::max(result.maxUpdatedAt, item.updatedAt);
      pageItems.push_back(std::move(item));
    }
    pageCount += 1;
    qDebug() << "CloudSync pull page:" << pageCount
             << "items=" << pageItems.size();
    result.pages.push_back(std::move(pageItems));

    cursor = root.value("next_last_evaluated_key").toString();
    if (cursor.isEmpty()) {
      result.ok = true;
      qDebug() << "CloudSync pull finished:" << "pages=" << pageCount
               << "maxUpdatedAt=" << result.maxUpdatedAt;
      return result;
    }
    waitForDelay(kPullPageGapMs);
  }
}

} // namespace CloudPlayStatsSync
