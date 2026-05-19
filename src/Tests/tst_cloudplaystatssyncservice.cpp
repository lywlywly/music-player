#include <QObject>
#include <QTest>

#include "../cloudplaystatssyncservice.h"

class TestCloudPlayStatsSync : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void pushIncrement_recordsDummyCall();
  void pushBulkIncrement_recordsAllDummyCallsAndResult();
  void pullDeltaPaged_streamsPagesAndComputesMaxUpdatedAt();
  void retryPolicy_retriesThrottleAndSucceeds();
  void retryPolicy_stopsAfterThrottleRetryLimit();
  void retryPolicy_retriesTimeoutAndFails();
};

void TestCloudPlayStatsSync::init() {
  qputenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC", "1");
  CloudPlayStatsSync::clearDummyPullPages();
  CloudPlayStatsSync::clearDummyPushCalls();
}

void TestCloudPlayStatsSync::cleanup() {
  CloudPlayStatsSync::clearDummyPullPages();
  CloudPlayStatsSync::clearDummyPushCalls();
  qunsetenv("MYPLAYER_CLOUD_SYNC_RETRY_DELAY_MS");
  qunsetenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC");
}

void TestCloudPlayStatsSync::pushIncrement_recordsDummyCall() {
  const bool ok = CloudPlayStatsSync::pushIncrement(
      "11111111-1111-1111-1111-111111111111", "song|artist|album", 2);

  QVERIFY(ok);

  const auto calls = CloudPlayStatsSync::dummyPushCalls();
  QCOMPARE(calls.size(), size_t(1));
  QCOMPARE(calls[0].userUuid, QString("11111111-1111-1111-1111-111111111111"));
  QCOMPARE(calls[0].songIdentityKey, std::string("song|artist|album"));
  QCOMPARE(calls[0].delta, 2);
}

void TestCloudPlayStatsSync::pushBulkIncrement_recordsAllDummyCallsAndResult() {
  CloudPlayStatsSync::setDummyPushResults({false});

  const bool ok = CloudPlayStatsSync::pushBulkIncrement(
      "11111111-1111-1111-1111-111111111111",
      {{"song-a|artist|album", 3}, {"song-b|artist|album", 1}});

  QVERIFY(!ok);

  const auto calls = CloudPlayStatsSync::dummyPushCalls();
  QCOMPARE(calls.size(), size_t(2));
  QCOMPARE(calls[0].songIdentityKey, std::string("song-a|artist|album"));
  QCOMPARE(calls[0].delta, 3);
  QCOMPARE(calls[1].songIdentityKey, std::string("song-b|artist|album"));
  QCOMPARE(calls[1].delta, 1);
}

void TestCloudPlayStatsSync::
    pullDeltaPaged_streamsPagesAndComputesMaxUpdatedAt() {
  CloudPlayStatItem item1{.songIdentityKey = "song-a|artist|album",
                          .playCount = 2,
                          .updatedAt = 111};
  CloudPlayStatItem item2{.songIdentityKey = "song-b|artist|album",
                          .playCount = 4,
                          .updatedAt = 222};
  CloudPlayStatsSync::setDummyPullPages({{item1}, {item2}}, true, 0);

  const CloudPlayStatsPullResult result = CloudPlayStatsSync::pullDeltaPaged(
      "11111111-1111-1111-1111-111111111111", 100, 50);

  QCOMPARE(result.pages.size(), size_t(2));
  QCOMPARE(result.pages[0].size() + result.pages[1].size(), size_t(2));
  QVERIFY(result.ok);
  QCOMPARE(result.maxUpdatedAt, 222);
}

void TestCloudPlayStatsSync::retryPolicy_retriesThrottleAndSucceeds() {
  qputenv("MYPLAYER_CLOUD_SYNC_RETRY_DELAY_MS", "0");

  const auto result = CloudPlayStatsSync::testingRunRetrySequence(
      {{.throttled = true}, {.ok = true}});

  QVERIFY(result.ok);
  QCOMPARE(result.attempts, 2);
}

void TestCloudPlayStatsSync::retryPolicy_stopsAfterThrottleRetryLimit() {
  qputenv("MYPLAYER_CLOUD_SYNC_RETRY_DELAY_MS", "0");

  const auto result =
      CloudPlayStatsSync::testingRunRetrySequence({{.throttled = true},
                                                   {.throttled = true},
                                                   {.throttled = true},
                                                   {.throttled = true},
                                                   {.ok = true}});

  QVERIFY(!result.ok);
  QCOMPARE(result.attempts, 4);
}

void TestCloudPlayStatsSync::retryPolicy_retriesTimeoutAndFails() {
  qputenv("MYPLAYER_CLOUD_SYNC_RETRY_DELAY_MS", "0");

  const auto result =
      CloudPlayStatsSync::testingRunRetrySequence({{.timedOut = true},
                                                   {.timedOut = true},
                                                   {.timedOut = true},
                                                   {.timedOut = true}});

  QVERIFY(!result.ok);
  QCOMPARE(result.attempts, 4);
}

QTEST_MAIN(TestCloudPlayStatsSync)
#include "tst_cloudplaystatssyncservice.moc"
