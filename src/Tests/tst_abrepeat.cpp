#include <QList>
#include <QObject>
#include <QTest>

#include "../pluginapi/iplayercontrol.h"
#include "../plugins/abrepeat/abrepeat.h"

namespace {
// Records the seeks it is asked for.
class RecordingControl : public IPlayerControl {
public:
  void seek(qint64 positionMs) override { seeks << positionMs; }

  QList<qint64> seeks;
};
} // namespace

class TestABRepeat : public QObject {
  Q_OBJECT

private slots:
  void positionAtOrPastB_seeksToA();
  void setBAtOrBeforeA_isIgnored();
  void setA_clearsB();
  void songStartAndStop_clearTheLoop();
};

void TestABRepeat::positionAtOrPastB_seeksToA() {
  RecordingControl control;
  ABRepeat repeat(control);
  repeat.trackStarted({});
  repeat.positionChanged(1000);
  repeat.setA();
  repeat.positionChanged(5000);
  repeat.setB();
  QCOMPARE(repeat.a(), std::optional<qint64>(1000));
  QCOMPARE(repeat.b(), std::optional<qint64>(5000));

  repeat.positionChanged(4900);
  QVERIFY(control.seeks.isEmpty());
  repeat.positionChanged(5000);
  QCOMPARE(control.seeks, QList<qint64>{1000});
  repeat.positionChanged(9000);
  QCOMPARE(control.seeks, (QList<qint64>{1000, 1000}));

  repeat.clear();
  repeat.positionChanged(9100);
  QCOMPARE(control.seeks.size(), 2);
}

void TestABRepeat::setBAtOrBeforeA_isIgnored() {
  RecordingControl control;
  ABRepeat repeat(control);
  repeat.trackStarted({});
  repeat.positionChanged(3000);
  repeat.setA();
  repeat.setB();
  QCOMPARE(repeat.b(), std::nullopt);
  repeat.positionChanged(2000);
  repeat.setB();
  QCOMPARE(repeat.b(), std::nullopt);
  QCOMPARE(repeat.a(), std::optional<qint64>(3000));
}

void TestABRepeat::setA_clearsB() {
  RecordingControl control;
  ABRepeat repeat(control);
  repeat.trackStarted({});
  repeat.positionChanged(1000);
  repeat.setA();
  repeat.positionChanged(2000);
  repeat.setB();
  repeat.positionChanged(1500);
  repeat.setA();
  QCOMPARE(repeat.a(), std::optional<qint64>(1500));
  QCOMPARE(repeat.b(), std::nullopt);
}

void TestABRepeat::songStartAndStop_clearTheLoop() {
  RecordingControl control;
  ABRepeat repeat(control);
  QVERIFY(!repeat.songLoaded());
  repeat.trackStarted({});
  QVERIFY(repeat.songLoaded());
  repeat.positionChanged(1000);
  repeat.setA();
  repeat.positionChanged(2000);
  repeat.setB();

  // Also the same song again.
  repeat.trackStarted({});
  QVERIFY(repeat.songLoaded());
  QCOMPARE(repeat.a(), std::nullopt);
  QCOMPARE(repeat.b(), std::nullopt);
  repeat.positionChanged(3000);
  QVERIFY(control.seeks.isEmpty());

  repeat.positionChanged(1000);
  repeat.setA();
  repeat.stopped();
  QVERIFY(!repeat.songLoaded());
  QCOMPARE(repeat.a(), std::nullopt);
}

QTEST_GUILESS_MAIN(TestABRepeat)
#include "tst_abrepeat.moc"
