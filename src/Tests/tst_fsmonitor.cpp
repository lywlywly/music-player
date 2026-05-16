#include <QDir>
#include <QObject>
#include <QTemporaryDir>
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
  explicit TestFSMonitor(QObject *parent = nullptr);
  FileSystemComparer fsComparer;
  IFileSystemMonitor *qFSMonitor = new QFileSystemMonitor{};
  IFileSystemMonitor *efswFSMonitor = new EFSWFileSystemMonitor{};
signals:
private slots:
  void testState();
  void testCompareTwoStates();
  void testFileSystemChange();
  void testQFSMonitor();
  void testEFSWFSMonitor();
};

TestFSMonitor::TestFSMonitor(QObject *parent) : QObject{parent} {}

void TestFSMonitor::testState() {
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  std::map<std::string, std::string> stateDict2 =
      fsComparer.getDirectoryState(tempDir.path().toStdString());
  QVERIFY(stateDict2.empty());
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
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QString basePath = tempDir.path();
  std::string toBeAddedFilename =
      QDir(basePath).filePath("exampleadd.mp3").toStdString();
  std::string toBeModifiedFilename =
      QDir(basePath).filePath("examplemodify.mp3").toStdString();
  std::string toBeDeletedFilename =
      QDir(basePath).filePath("exampledelete.mp3").toStdString();
  std::remove(toBeAddedFilename.c_str());
  std::ofstream outputFile0(toBeDeletedFilename);
  if (outputFile0.is_open()) {
    outputFile0 << "This is some text written to the file." << std::endl;
    outputFile0.close();
  }
  std::ofstream outputFile1(toBeModifiedFilename);
  if (outputFile1.is_open()) {
    outputFile1 << "Initial content." << std::endl;
    outputFile1.close();
  }

  std::map<std::string, std::string> stateDict1 =
      fsComparer.getDirectoryState(basePath.toStdString());

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
      fsComparer.getDirectoryState(basePath.toStdString());

  auto [removed, added, changed] =
      fsComparer.compareTwoStates(stateDict1, stateDict2);
  QCOMPARE(removed, std::vector<std::string>{toBeDeletedFilename});
  QCOMPARE(added, std::vector<std::string>{toBeAddedFilename});
  QCOMPARE(changed, std::vector<std::string>{toBeModifiedFilename});
}

void TestFSMonitor::testQFSMonitor() {
  QSignalSpy spy(dynamic_cast<QObject *>(qFSMonitor),
                 SIGNAL(directoryChanged(const QString &)));

  qFSMonitor->addWatchingPath("/home/luyao/Documents/test/");

  std::string toBeAddedFilename = "/home/luyao/Documents/test/exampleadd.txt";
  std::remove(toBeAddedFilename.c_str());
  std::ofstream outputFile(toBeAddedFilename);
  if (outputFile.is_open()) {
    outputFile << "This is some text written to the file." << std::endl;
    outputFile.close();
  }

  QTest::qWait(1000);

  qDebug() << spy.count();
  // QCOMPARE(spy.takeLast().at(0).toString(), "/home/luyao/Documents/test/");
}

void TestFSMonitor::testEFSWFSMonitor() {
  QSignalSpy spy(dynamic_cast<QObject *>(efswFSMonitor),
                 SIGNAL(fileChanged(const QString &)));

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
  // QCOMPARE(spy.takeLast().at(0).toString(), "exampleadd.txt");
}

QTEST_MAIN(TestFSMonitor)
#include "tst_fsmonitor.moc"
