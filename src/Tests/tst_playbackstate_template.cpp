#include <QObject>
#include <QTest>

class TestClass : public QObject {
  Q_OBJECT
public:
  explicit TestClass(QObject *parent = nullptr);

signals:
private slots:
  void test1();
};

TestClass::TestClass(QObject *parent) : QObject{parent} {}
void TestPlaybackState::test1() {
}

QTEST_MAIN(TestClass)
#include "tst_.moc"
