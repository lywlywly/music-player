#include <QObject>
#include <QTest>

#include "../filesystemcomparer.h"
#include "../songtablemodel.h"

class TestSongTableModel : public QObject {
  Q_OBJECT
 public:
  explicit TestSongTableModel(QObject *parent = nullptr);
  SongTableModel *model;
  SongLoader loader;

 signals:
 private slots:
  void toUpper();
  void testLoad();
  void testRenew();
};

TestSongTableModel::TestSongTableModel(QObject *parent) : QObject{parent} {
  model = new SongTableModel{this};
}
void TestSongTableModel::toUpper() {
  QString str = "Hello";
  QVERIFY(str.toUpper() == "HELLO");
}

void TestSongTableModel::testLoad() {
  std::string directoryPath = "/home/luyao/Videos/";
  std::map<std::string, std::string> state1 = loader.loadSongHash();

  FileSystemComparer fsComparer;
  std::map<std::string, std::string> state2 =
      fsComparer.getDirectoryState(directoryPath);
  auto [removed, added, changed] = fsComparer.compareTwoStates(state1, state2);

  qDebug() << "removed";
  for (std::string a : removed) {
    qDebug() << a;
  }
  qDebug() << "added";
  for (std::string a : added) {
    qDebug() << QString::fromStdString(a);
  }
  qDebug() << "changed";
  for (std::string a : changed) {
    qDebug() << a;
  }

  // loader.saveHashToDB(state2);
}

void TestSongTableModel::testRenew()
{
  // loader.renewHash();
  // loader.insertSongMetadataToDB("/home/luyao/Music/YOASOBI - ハルカ - 00 - ハルカ.mp3");
  loader.renewHash();
}

QTEST_MAIN(TestSongTableModel)
#include "tst_songtablemodel.moc"
