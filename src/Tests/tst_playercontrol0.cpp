#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include "../playercontrolmodel.h"
#include "../songtablemodel.h"

class TestPlayerControl : public QObject {
  Q_OBJECT
 public:
  explicit TestPlayerControl(QObject *parent = nullptr);
  SongTableModel *model = new SongTableModel{this};
  MyProxyModel *proxy = new MyProxyModel{this};
  PlayerControlModel *control = new PlayerControlModel{proxy};
 signals:
 private slots:
  void test1();
  void test2();
};

TestPlayerControl::TestPlayerControl(QObject *parent) : QObject{parent} {
  proxy->setSourceModel(model);
  model->appendSong(
      QUrl{"/home/luyao/Music/ACAね、Rin音、Yaffle - "
           "Character - 00 - Character.mp3"});
  model->appendSong(
      QUrl{"/home/luyao/Music/Adele - Easy On Me - 02 - Easy On Me.flac"});
}

void TestPlayerControl::test1() {
  QSignalSpy spy(control, SIGNAL(indexChange(int)));
  control->next();
  QCOMPARE(spy.count(), 1);
  QList<QVariant> arguments = spy.takeFirst();
  QCOMPARE(arguments.at(0).toInt(), 0);
}

void TestPlayerControl::test2() {
  QSignalSpy spy(control, SIGNAL(indexChange(int)));
  control->next();
  QCOMPARE(spy.count(), 1);
  QList<QVariant> arguments = spy.takeFirst();
  QCOMPARE(arguments.at(0).toInt(), 2);
}

QTEST_MAIN(TestPlayerControl)
#include "tst_playercontrol.moc"
