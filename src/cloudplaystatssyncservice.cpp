#include "cloudplaystatssyncservice.h"
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <memory>

namespace {
constexpr auto kCloudPlayStatsUrl =
    "https://y2khecz4nrzrqjy4t556erix2e0hcqrm.lambda-url.us-east-2.on.aws/";
constexpr int kThrottleRetryDelayMs = 60000;
constexpr int kPullPageGapMs = 1000;

#ifdef MYPLAYER_TESTING
struct DummyCloudSyncState {
  std::vector<std::vector<CloudPlayStatItem>> pullPages;
  bool pullOk = true;
  qint64 pullMaxUpdatedAt = 0;
  std::vector<CloudPlayStatsSyncService::DummyPushCall> pushCalls;
  std::vector<bool> pushResults;
};

DummyCloudSyncState &dummyState() {
  static DummyCloudSyncState state;
  return state;
}

bool useDummyCloudSync() {
  return qEnvironmentVariableIntValue("MYPLAYER_USE_DUMMY_CLOUD_SYNC") != 0;
}
#endif

void finishBoolCallback(const std::function<void(bool ok)> &cb, bool ok) {
  if (cb) {
    cb(ok);
  }
}

#ifdef MYPLAYER_TESTING
bool handleDummyPush(const std::function<void()> &record,
                     const std::function<void(bool ok)> &onFinished) {
  if (!useDummyCloudSync()) {
    return false;
  }
  record();
  bool ok = true;
  if (!dummyState().pushResults.empty()) {
    ok = dummyState().pushResults.front();
    dummyState().pushResults.erase(dummyState().pushResults.begin());
  }
  finishBoolCallback(onFinished, ok);
  return true;
}
#endif

#ifdef MYPLAYER_TESTING
bool maybeHandleDummyPull(
    const QString &userUuid, qint64 updatedAfter, int pageLimit,
    const std::function<void(const std::vector<CloudPlayStatItem> &)> &onPage,
    const std::function<void(bool ok, qint64 maxUpdatedAt)> &onFinished) {
  if (useDummyCloudSync()) {
    if (onPage) {
      for (const auto &page : dummyState().pullPages) {
        onPage(page);
      }
    }
    if (onFinished) {
      qint64 maxUpdatedAt = dummyState().pullMaxUpdatedAt;
      if (maxUpdatedAt == 0) {
        for (const auto &page : dummyState().pullPages) {
          for (const CloudPlayStatItem &item : page) {
            maxUpdatedAt = std::max(maxUpdatedAt, item.updatedAt);
          }
        }
      }
      onFinished(dummyState().pullOk, maxUpdatedAt);
    }
    return true;
  }
  return false;
}
#endif
} // namespace

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

CloudPlayStatsSyncService::CloudPlayStatsSyncService(QObject *parent)
    : QObject(parent) {}

#ifdef MYPLAYER_TESTING
void CloudPlayStatsSyncService::setDummyPullPages(
    const std::vector<std::vector<CloudPlayStatItem>> &pages, bool ok,
    qint64 maxUpdatedAt) {
  dummyState().pullPages = pages;
  dummyState().pullOk = ok;
  dummyState().pullMaxUpdatedAt = maxUpdatedAt;
}

void CloudPlayStatsSyncService::clearDummyPullPages() {
  dummyState().pullPages.clear();
  dummyState().pullOk = true;
  dummyState().pullMaxUpdatedAt = 0;
}

std::vector<CloudPlayStatsSyncService::DummyPushCall>
CloudPlayStatsSyncService::dummyPushCalls() {
  return dummyState().pushCalls;
}

void CloudPlayStatsSyncService::clearDummyPushCalls() {
  dummyState().pushCalls.clear();
  dummyState().pushResults.clear();
}

void CloudPlayStatsSyncService::setDummyPushResults(
    const std::vector<bool> &results) {
  dummyState().pushResults = results;
}
#endif

void CloudPlayStatsSyncService::pushIncrement(
    const QString &userUuid, const std::string &songIdentityKey, int delta) {
  pushIncrement(userUuid, songIdentityKey, delta, {});
}

void CloudPlayStatsSyncService::pushIncrement(
    const QString &userUuid, const std::string &songIdentityKey, int delta,
    const std::function<void(bool ok)> &onFinished) {
#ifdef MYPLAYER_TESTING
  if (handleDummyPush(
          [&]() {
            dummyState().pushCalls.push_back(
                {userUuid, songIdentityKey, delta});
          },
          onFinished)) {
    return;
  }
#endif

  if (userUuid.trimmed().isEmpty() || songIdentityKey.empty() || delta <= 0) {
    qWarning() << "CloudSync push skipped: invalid args"
               << "uuidEmpty=" << userUuid.trimmed().isEmpty()
               << "identityEmpty=" << songIdentityKey.empty()
               << "delta=" << delta;
    finishBoolCallback(onFinished, false);
    return;
  }
  qDebug() << "CloudSync push begin:" << "identityKey="
           << QString::fromStdString(songIdentityKey) << "delta=" << delta;

  QNetworkRequest request(QUrl(QString::fromUtf8(kCloudPlayStatsUrl)));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  QJsonObject body{
      {"user_uuid", userUuid},
      {"song_identity_key", QString::fromStdString(songIdentityKey)},
      {"delta", delta},
  };
  QNetworkReply *reply = network_.post(request, QJsonDocument(body).toJson());
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, userUuid, songIdentityKey, delta, onFinished]() {
            const QByteArray body = reply->readAll();
            if (isThrottleResponse(reply, body)) {
              qWarning() << "cloud push throttled; retry in 60s";
              reply->deleteLater();
              QTimer::singleShot(
                  kThrottleRetryDelayMs, this,
                  [this, userUuid, songIdentityKey, delta, onFinished]() {
                    pushIncrement(userUuid, songIdentityKey, delta, onFinished);
                  });
              return;
            }

            const bool ok = reply->error() == QNetworkReply::NoError;
            if (!ok) {
              qWarning() << "cloud push failed:" << reply->errorString();
            } else {
              qDebug() << "CloudSync push success";
            }
            finishBoolCallback(onFinished, ok);
            reply->deleteLater();
          });
}

void CloudPlayStatsSyncService::pushBulkIncrement(
    const QString &userUuid,
    const std::vector<std::pair<std::string, int>> &updates,
    const std::function<void(bool ok)> &onFinished) {
#ifdef MYPLAYER_TESTING
  if (handleDummyPush(
          [&]() {
            for (const auto &[songIdentityKey, delta] : updates) {
              dummyState().pushCalls.push_back(
                  {userUuid, songIdentityKey, delta});
            }
          },
          onFinished)) {
    return;
  }
#endif

  if (userUuid.trimmed().isEmpty() || updates.empty()) {
    qWarning() << "CloudSync bulk push skipped: invalid args"
               << "uuidEmpty=" << userUuid.trimmed().isEmpty()
               << "updates=" << updates.size();
    finishBoolCallback(onFinished, false);
    return;
  }
  if (updates.size() > 200) {
    qWarning() << "CloudSync bulk push skipped: updates > 200";
    finishBoolCallback(onFinished, false);
    return;
  }
  QJsonArray updatesArray;
  for (const auto &[songIdentityKey, delta] : updates) {
    if (songIdentityKey.empty() || delta <= 0) {
      qWarning() << "CloudSync bulk push skipped: invalid update item";
      finishBoolCallback(onFinished, false);
      return;
    }
    updatesArray.push_back(QJsonObject{
        {"song_identity_key", QString::fromStdString(songIdentityKey)},
        {"delta", delta},
    });
  }
  qDebug() << "CloudSync bulk push begin: updates=" << updates.size();

  QNetworkRequest request(QUrl(QString::fromUtf8(kCloudPlayStatsUrl)));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  QJsonObject body{
      {"user_uuid", userUuid},
      {"updates", updatesArray},
  };
  QNetworkReply *reply = network_.post(request, QJsonDocument(body).toJson());
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, userUuid, updates, onFinished]() {
            const QByteArray body = reply->readAll();
            if (isThrottleResponse(reply, body)) {
              qWarning() << "cloud bulk push throttled; retry in 60s";
              reply->deleteLater();
              QTimer::singleShot(kThrottleRetryDelayMs, this,
                                 [this, userUuid, updates, onFinished]() {
                                   pushBulkIncrement(userUuid, updates,
                                                     onFinished);
                                 });
              return;
            }

            const bool ok = reply->error() == QNetworkReply::NoError;
            if (!ok) {
              qWarning() << "cloud bulk push failed:" << reply->errorString();
            } else {
              qDebug() << "CloudSync bulk push success";
            }
            finishBoolCallback(onFinished, ok);
            reply->deleteLater();
          });
}

void CloudPlayStatsSyncService::pullDeltaPaged(
    const QString &userUuid, qint64 updatedAfter, int pageLimit,
    const std::function<void(const std::vector<CloudPlayStatItem> &)> &onPage,
    const std::function<void(bool ok, qint64 maxUpdatedAt)> &onFinished) {
#ifdef MYPLAYER_TESTING
  if (maybeHandleDummyPull(userUuid, updatedAfter, pageLimit, onPage,
                           onFinished)) {
    return;
  }
#endif

  if (userUuid.trimmed().isEmpty() || pageLimit <= 0 || pageLimit > 200) {
    qWarning() << "CloudSync pull skipped: invalid args"
               << "uuidEmpty=" << userUuid.trimmed().isEmpty()
               << "updatedAfter=" << updatedAfter << "pageLimit=" << pageLimit;
    if (onFinished) {
      onFinished(false, 0);
    }
    return;
  }
  qDebug() << "CloudSync pull begin:" << "updatedAfter=" << updatedAfter
           << "pageLimit=" << pageLimit;

  auto maxSeenUpdatedAt = std::make_shared<qint64>(0);
  auto pageCount = std::make_shared<int>(0);
  auto fetchPage = std::make_shared<std::function<void(const QString &)>>();
  *fetchPage = [this, userUuid, updatedAfter, pageLimit, onPage, onFinished,
                maxSeenUpdatedAt, pageCount, fetchPage](const QString &cursor) {
    QUrl url(QString::fromUtf8(kCloudPlayStatsUrl));
    QUrlQuery query;
    query.addQueryItem("user_uuid", userUuid);
    query.addQueryItem("updated_after", QString::number(updatedAfter));
    query.addQueryItem("limit", QString::number(pageLimit));
    if (!cursor.isEmpty()) {
      query.addQueryItem("last_evaluated_key", cursor);
    }
    url.setQuery(query);

    QNetworkReply *reply = network_.get(QNetworkRequest(url));
    connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, onPage, onFinished, maxSeenUpdatedAt, pageCount,
         fetchPage, cursor]() mutable {
          const QByteArray body = reply->readAll();
          if (isThrottleResponse(reply, body)) {
            qWarning() << "cloud pull throttled; retry in 60s";
            reply->deleteLater();
            QTimer::singleShot(kThrottleRetryDelayMs, this,
                               [fetchPage, cursor]() { (*fetchPage)(cursor); });
            return;
          }
          if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "cloud pull failed:" << reply->errorString();
            if (onFinished) {
              onFinished(false, *maxSeenUpdatedAt);
            }
            reply->deleteLater();
            return;
          }

          QJsonParseError parseError;
          const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
          if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "cloud pull json parse error:"
                       << parseError.errorString();
            if (onFinished) {
              onFinished(false, *maxSeenUpdatedAt);
            }
            reply->deleteLater();
            return;
          }

          const QJsonObject root = doc.object();
          if (!root.value("ok").toBool()) {
            qWarning() << "cloud pull api error:" << root;
            if (onFinished) {
              onFinished(false, *maxSeenUpdatedAt);
            }
            reply->deleteLater();
            return;
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
            *maxSeenUpdatedAt = std::max(*maxSeenUpdatedAt, item.updatedAt);
            pageItems.push_back(std::move(item));
          }
          *pageCount += 1;
          qDebug() << "CloudSync pull page:" << *pageCount
                   << "items=" << pageItems.size();
          if (onPage) {
            onPage(pageItems);
          }

          const QString nextCursor =
              root.value("next_last_evaluated_key").toString();
          reply->deleteLater();

          if (nextCursor.isEmpty()) {
            if (onFinished) {
              qDebug() << "CloudSync pull finished:" << "pages=" << *pageCount
                       << "maxUpdatedAt=" << *maxSeenUpdatedAt;
              onFinished(true, *maxSeenUpdatedAt);
            }
            return;
          }
          QTimer::singleShot(kPullPageGapMs, this, [fetchPage, nextCursor]() {
            (*fetchPage)(nextCursor);
          });
        });
  };

  (*fetchPage)(QString());
}
