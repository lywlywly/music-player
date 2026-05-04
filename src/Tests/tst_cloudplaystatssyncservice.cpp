#include <QObject>
#include <QTest>

#include "../cloudplaystatssyncservice.h"

class TestCloudPlayStatsSyncService : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void pushIncrement_recordsDummyCall();
  void pushBulkIncrement_recordsAllDummyCallsAndResult();
  void pullDeltaPaged_streamsPagesAndComputesMaxUpdatedAt();

private:
  CloudPlayStatsSyncService *service_ = nullptr;
};

void TestCloudPlayStatsSyncService::init() {
  qputenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC", "1");
  CloudPlayStatsSyncService::clearDummyPullPages();
  CloudPlayStatsSyncService::clearDummyPushCalls();
  service_ = new CloudPlayStatsSyncService();
}

void TestCloudPlayStatsSyncService::cleanup() {
  delete service_;
  service_ = nullptr;
  CloudPlayStatsSyncService::clearDummyPullPages();
  CloudPlayStatsSyncService::clearDummyPushCalls();
  qunsetenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC");
}

void TestCloudPlayStatsSyncService::pushIncrement_recordsDummyCall() {
  bool callbackCalled = false;
  bool callbackOk = false;

  service_->pushIncrement("11111111-1111-1111-1111-111111111111",
                          "song|artist|album", 2, [&](bool ok) {
                            callbackCalled = true;
                            callbackOk = ok;
                          });

  QVERIFY(callbackCalled);
  QVERIFY(callbackOk);

  const auto calls = CloudPlayStatsSyncService::dummyPushCalls();
  QCOMPARE(calls.size(), size_t(1));
  QCOMPARE(calls[0].userUuid, QString("11111111-1111-1111-1111-111111111111"));
  QCOMPARE(calls[0].songIdentityKey, std::string("song|artist|album"));
  QCOMPARE(calls[0].delta, 2);
}

void TestCloudPlayStatsSyncService::
    pushBulkIncrement_recordsAllDummyCallsAndResult() {
  CloudPlayStatsSyncService::setDummyPushResults({false});

  bool callbackCalled = false;
  bool callbackOk = true;
  service_->pushBulkIncrement(
      "11111111-1111-1111-1111-111111111111",
      {{"song-a|artist|album", 3}, {"song-b|artist|album", 1}}, [&](bool ok) {
        callbackCalled = true;
        callbackOk = ok;
      });

  QVERIFY(callbackCalled);
  QVERIFY(!callbackOk);

  const auto calls = CloudPlayStatsSyncService::dummyPushCalls();
  QCOMPARE(calls.size(), size_t(2));
  QCOMPARE(calls[0].songIdentityKey, std::string("song-a|artist|album"));
  QCOMPARE(calls[0].delta, 3);
  QCOMPARE(calls[1].songIdentityKey, std::string("song-b|artist|album"));
  QCOMPARE(calls[1].delta, 1);
}

void TestCloudPlayStatsSyncService::
    pullDeltaPaged_streamsPagesAndComputesMaxUpdatedAt() {
  CloudPlayStatItem item1{.songIdentityKey = "song-a|artist|album",
                          .playCount = 2,
                          .updatedAt = 111};
  CloudPlayStatItem item2{.songIdentityKey = "song-b|artist|album",
                          .playCount = 4,
                          .updatedAt = 222};
  CloudPlayStatsSyncService::setDummyPullPages({{item1}, {item2}}, true, 0);

  int pageCount = 0;
  int itemCount = 0;
  bool finishedCalled = false;
  bool finishedOk = false;
  qint64 finishedMaxUpdatedAt = 0;

  service_->pullDeltaPaged(
      "11111111-1111-1111-1111-111111111111", 100, 50,
      [&](const std::vector<CloudPlayStatItem> &items) {
        pageCount += 1;
        itemCount += static_cast<int>(items.size());
      },
      [&](bool ok, qint64 maxUpdatedAt) {
        finishedCalled = true;
        finishedOk = ok;
        finishedMaxUpdatedAt = maxUpdatedAt;
      });

  QCOMPARE(pageCount, 2);
  QCOMPARE(itemCount, 2);
  QVERIFY(finishedCalled);
  QVERIFY(finishedOk);
  QCOMPARE(finishedMaxUpdatedAt, 222);
}

QTEST_MAIN(TestCloudPlayStatsSyncService)
#include "tst_cloudplaystatssyncservice.moc"
