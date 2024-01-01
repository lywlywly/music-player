#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include "../playercontrolmodel.h"
#include "../songtablemodel.h"

class TestPlayList : public QObject, virtual public IPlayList {
  Q_OBJECT
  Q_INTERFACES(IPlayList)
  QList<int> getSourceIndices() override {
    QList<int> l = {0, 1, 2, 3, 4};
    return l;
  };
  QUrl getUrl(int) override { return QUrl{"url"}; };
 signals:
  void playlistChanged() override;
};

class TestPlayerControl : public QObject {
  Q_OBJECT
 public:
  explicit TestPlayerControl(QObject *parent = nullptr);
  TestPlayList tp;
  // PlayerControlModel *control = new PlayerControlModel{&tp};
 signals:
 private slots:
  void test1();
  void test2();
  void test3();
  void test4();
  void multiQueue();
};

TestPlayerControl::TestPlayerControl(QObject *parent) : QObject{parent} {}

void TestPlayerControl::test1() {
  PlayerControlModel *control = new PlayerControlModel{&tp};
  control->onPlayListChange();
  QSignalSpy spy(control, SIGNAL(indexChange(int)));

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 0);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 1);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 2);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 3);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 4);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 0);
}

void TestPlayerControl::test2() {
  PlayerControlModel *control = new PlayerControlModel{&tp};
  control->onPlayListChange();
  control->addToQueue(3);
  QSignalSpy spy(control, SIGNAL(indexChange(int)));

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 3);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 4);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 0);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 1);
}

void TestPlayerControl::test3() {
  PlayerControlModel *control = new PlayerControlModel{&tp};
  control->onPlayListChange();
  control->setNext(3);
  QSignalSpy spy(control, SIGNAL(indexChange(int)));
  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 3);
  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 4);
  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 0);
  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 1);
}

void TestPlayerControl::test4() {
  PlayerControlModel *control = new PlayerControlModel{&tp};
  control->onPlayListChange();
  QSignalSpy spy(control, SIGNAL(indexChange(int)));

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 0);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 1);

  control->setNext(3);
  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 3);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 4);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 0);
}

void TestPlayerControl::multiQueue() {
  PlayerControlModel *control = new PlayerControlModel{&tp};
  control->onPlayListChange();
  control->addToQueue(3);
  control->addToQueue(3);
  control->addToQueue(3);
  control->addToQueue(1);
  QSignalSpy spy(control, SIGNAL(indexChange(int)));

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 3);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 3);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 3);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 1);

  control->next();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeLast().at(0).toInt(), 2);
}

QTEST_MAIN(TestPlayerControl)
#include "tst_playercontrol.moc"
