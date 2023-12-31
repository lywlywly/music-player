#include <QObject>
#include <QTest>

class TestQString : public QObject {
  Q_OBJECT
 public:
  explicit TestQString(QObject *parent = nullptr);

 signals:
 private slots:
  void toUpper();
};

TestQString::TestQString(QObject *parent) : QObject{parent} {}
void TestQString::toUpper() {
  QString str = "Hello";
  QVERIFY(str.toUpper() == "HELLO");
}

QTEST_MAIN(TestQString)
#include "tst_basics.moc"
