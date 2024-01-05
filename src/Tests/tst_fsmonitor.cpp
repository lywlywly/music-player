#include <QObject>
#include <QTest>
#include <cstdio>
#include <fstream>

#include "../filesystemmonitor.h"

class TestFSMonitor : public QObject {
  Q_OBJECT
 public:
  explicit TestFSMonitor(QObject* parent = nullptr);
  FileSystemMonitor fsMonitor;

 signals:
 private slots:
  void toUpper();
  void testCompareTwoStates();
  void testFileSystemChange();
};

TestFSMonitor::TestFSMonitor(QObject* parent) : QObject{parent} {}
void TestFSMonitor::toUpper() {
  QString str = "Hello";
  QVERIFY(str.toUpper() == "HELLO");
}

void TestFSMonitor::testCompareTwoStates() {
  std::map<std::string, std::string> stateDict1 = {
      {"file1", "1"}, {"file2", "1"}, {"file3", "1"}};
  std::map<std::string, std::string> stateDict2 = {
      {"file1", "1"}, {"file2", "2"}, {"file4", "1"}};

  auto [removed, added, changed] =
      fsMonitor.compareTwoStates(stateDict1, stateDict2);
  QVERIFY(removed == std::vector<std::string>{"file3"});
  QVERIFY(added == std::vector<std::string>{"file4"});
  QVERIFY(changed == std::vector<std::string>{"file2"});
}

void TestFSMonitor::testFileSystemChange() {
  std::string toBeAddedFilename = "/home/luyao/Documents/test/exampleadd.txt";
  std::string toBeModifiedFilename =
      "/home/luyao/Documents/test/examplemodify.txt";
  std::string toBeDeletedFilename =
      "/home/luyao/Documents/test/exampledelete.txt";
  std::remove(toBeAddedFilename.c_str());
  std::ofstream outputFile0(toBeDeletedFilename);
  if (outputFile0.is_open()) {
    outputFile0 << "This is some text written to the file." << std::endl;
    outputFile0.close();
  }

  std::map<std::string, std::string> stateDict1 =
      fsMonitor.getDirectoryState("/home/luyao/Documents/test/");

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
      fsMonitor.getDirectoryState("/home/luyao/Documents/test/");

  auto [removed, added, changed] =
      fsMonitor.compareTwoStates(stateDict1, stateDict2);
  QCOMPARE(removed, std::vector<std::string>{"exampledelete.txt"});
  QCOMPARE(added, std::vector<std::string>{"exampleadd.txt"});
  QCOMPARE(changed, std::vector<std::string>{"examplemodify.txt"});
}

QTEST_MAIN(TestFSMonitor)
#include "tst_fsmonitor.moc"
