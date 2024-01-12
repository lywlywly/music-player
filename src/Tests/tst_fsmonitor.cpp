#include <QObject>
#include <QTest>
#include <cstdio>
#include <fstream>

#include "../efswfilesystemmonitor.h"
#include "../filesystemcomparer.h"
#include "../qfilesystemmonitor.h"
#include "qsignalspy.h"

class TestFSMonitor : public QObject {
  Q_OBJECT
 public:
  explicit TestFSMonitor(QObject* parent = nullptr);
  FileSystemComparer fsComparer;
  IFileSystemMonitor* qFSMonitor = new QFileSystemMonitor{};
  IFileSystemMonitor* efswFSMonitor = new EFSWFileSystemMonitor{};
 signals:
 private slots:
  void testState();
  void testCompareTwoStates();
  void testFileSystemChange();
  void testQFSMonitor();
  void testEFSWFSMonitor();
};

TestFSMonitor::TestFSMonitor(QObject* parent) : QObject{parent} {}

void TestFSMonitor::testState()
{
  std::map<std::string, std::string> stateDict2 =
      fsComparer.getDirectoryState("/home/luyao/Documents/test/");
}

void TestFSMonitor::testCompareTwoStates() {
  std::map<std::string, std::string> stateDict1 = {
      {"file1", "1"}, {"file2", "1"}, {"file3", "1"}};
  std::map<std::string, std::string> stateDict2 = {
      {"file1", "1"}, {"file2", "2"}, {"file4", "1"}};

  auto [removed, added, changed] =
      fsComparer.compareTwoStates(stateDict1, stateDict2);
  QVERIFY(removed == std::vector<std::string>{"file3"});
  QVERIFY(added == std::vector<std::string>{"file4"});
  QVERIFY(changed == std::vector<std::string>{"file2"});
}

void TestFSMonitor::testFileSystemChange() {
  std::string toBeAddedFilename = "/home/luyao/Documents/test/exampleadd.mp3";
  std::string toBeModifiedFilename =
      "/home/luyao/Documents/test/examplemodify.mp3";
  std::string toBeDeletedFilename =
      "/home/luyao/Documents/test/exampledelete.mp3";
  std::remove(toBeAddedFilename.c_str());
  std::ofstream outputFile0(toBeDeletedFilename);
  if (outputFile0.is_open()) {
    outputFile0 << "This is some text written to the file." << std::endl;
    outputFile0.close();
  }

  std::map<std::string, std::string> stateDict1 =
      fsComparer.getDirectoryState("/home/luyao/Documents/test/");

  std::ofstream outputFile(toBeAddedFilename);
  if (outputFile.is_open()) {
    outputFile << "This is some text written to the file." << std::endl;
    outputFile.close();
  }

  std::ofstream outputFile2(toBeModifiedFilename, std::ios::app);
  if (outputFile2.is_open()) {
    outputFile2 << "This is some text written to the file." << std::endl;
    outputFile2.close();
  }

  std::remove(toBeDeletedFilename.c_str());

  std::map<std::string, std::string> stateDict2 =
      fsComparer.getDirectoryState("/home/luyao/Documents/test/");

  auto [removed, added, changed] =
      fsComparer.compareTwoStates(stateDict1, stateDict2);
  QCOMPARE(removed, std::vector<std::string>{"/home/luyao/Documents/test/exampledelete.mp3"});
  QCOMPARE(added, std::vector<std::string>{"/home/luyao/Documents/test/exampleadd.mp3"});
  QCOMPARE(changed, std::vector<std::string>{"/home/luyao/Documents/test/examplemodify.mp3"});
}

void TestFSMonitor::testQFSMonitor() {
  QSignalSpy spy(dynamic_cast<QObject*>(qFSMonitor),
                 SIGNAL(directoryChanged(const QString&)));

  qFSMonitor->addWatchingPath("/home/luyao/Documents/test/");

  std::string toBeAddedFilename = "/home/luyao/Documents/test/exampleadd.txt";
  std::remove(toBeAddedFilename.c_str());
  std::ofstream outputFile(toBeAddedFilename);
  if (outputFile.is_open()) {
    outputFile << "This is some text written to the file." << std::endl;
    outputFile.close();
  }

  QTest::qWait(100);

  qDebug() << spy.count();
  QCOMPARE(spy.takeLast().at(0).toString(), "/home/luyao/Documents/test/");
}

void TestFSMonitor::testEFSWFSMonitor() {
  QSignalSpy spy(dynamic_cast<QObject*>(efswFSMonitor),
                 SIGNAL(fileChanged(const QString&)));

  efswFSMonitor->addWatchingPath("/home/luyao/Documents/test/");

  std::string toBeAddedFilename = "/home/luyao/Documents/test/exampleadd.txt";
  std::remove(toBeAddedFilename.c_str());
  std::ofstream outputFile(toBeAddedFilename);
  if (outputFile.is_open()) {
    outputFile << "This is some text written to the file." << std::endl;
    outputFile.close();
  }

  QTest::qWait(100);

  qDebug() << spy.count();
  QCOMPARE(spy.takeLast().at(0).toString(), "exampleadd.txt");
}

QTEST_MAIN(TestFSMonitor)
#include "tst_fsmonitor.moc"
