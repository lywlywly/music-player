#include <QObject>
#include <QProcess>
#include <QTest>
#include <chrono>
#include <filesystem>

#include "../checksumcalculator.h"
namespace fs = std::filesystem;

class TestChecksum : public QObject {
  Q_OBJECT
 public:
  explicit TestChecksum(QObject* parent = nullptr);
  ChecksumCalculator checksumCalc;

 signals:
 private slots:
  void toUpper();
  void testTextFile();
  void testTextPart();
  void testTextPart2();
  void testSongFull();
  void testSongHeader();
  void testPerformance();
};

TestChecksum::TestChecksum(QObject* parent) : QObject{parent} {}
void TestChecksum::toUpper() {
  QString str = "Hello";
  QVERIFY(str.toUpper() == "HELLO");
}

void TestChecksum::testTextFile() {
  std::string sha1 =
      checksumCalc.calculateSHA1("/home/luyao/Documents/test/test1.txt");
  QVERIFY(sha1 == "0764dc36263ea5aa38ad31803af25e48");
}

void TestChecksum::testTextPart() {
  std::string sha1_01 = checksumCalc.calculateHeaderSHA1(
      "/home/luyao/Documents/test/large1.txt", 1);
  std::string sha1_02 = checksumCalc.calculateHeaderSHA1(
      "/home/luyao/Documents/test/large2.txt", 1);
  QVERIFY(sha1_01 == sha1_02);
}

void TestChecksum::testTextPart2() {
  std::string sha1_01 = checksumCalc.calculateHeaderSHA1(
      "/home/luyao/Documents/test/large1.txt", 1);
  std::string sha1_02 = checksumCalc.calculateHeaderSHA1(
      "/home/luyao/Documents/test/large3.txt", 1);
  QVERIFY(sha1_01 != sha1_02);
}

void TestChecksum::testSongFull() {
  std::string sha1_01 = checksumCalc.calculateSHA1(
      "/home/luyao/Documents/test/陶喆 - 陶喆同名专辑 - 00 - 流沙.mp3");
  QProcess process;
  process.start(
      "ffmpeg",
      QStringList()
          << "-i"
          << "/home/luyao/Documents/test/陶喆 - 陶喆同名专辑 - 00 - 流沙.mp3"
          << "-metadata"
          << "title=流顺沙"
          << "-c"
          << "copy"
          << "/home/luyao/Documents/test/陶喆 - 陶喆同名专辑 - 00 - 流顺沙.mp3"
          << "-y");
  process.waitForFinished();
  std::string sha1_02 = checksumCalc.calculateSHA1(
      "/home/luyao/Documents/test/陶喆 - 陶喆同名专辑 - 00 - 流顺沙.mp3");
  QVERIFY(sha1_01 != sha1_02);
}

// only calculate first 8196 bytes since meta data is in the front
// TODO: verify only checking first 8196 bytes is safe
void TestChecksum::testSongHeader() {
  std::string sha1_01 = checksumCalc.calculateHeaderSHA1(
      "/home/luyao/Documents/test/陶喆 - 陶喆同名专辑 - 00 - 流沙.mp3", 10);
  QProcess process;
  process.start(
      "ffmpeg",
      QStringList()
          << "-i"
          << "/home/luyao/Documents/test/陶喆 - 陶喆同名专辑 - 00 - 流沙.mp3"
          << "-metadata"
          << "title=流顺沙"
          << "-c"
          << "copy"
          << "/home/luyao/Documents/test/陶喆 - 陶喆同名专辑 - 00 - 流顺沙.mp3"
          << "-y");
  process.waitForFinished();
  std::string sha1_02 = checksumCalc.calculateHeaderSHA1(
      "/home/luyao/Documents/test/陶喆 - 陶喆同名专辑 - 00 - 流顺沙.mp3", 10);
  QVERIFY(sha1_01 != sha1_02);
}

void TestChecksum::testPerformance() {
  std::string directoryPath = "/Users/wangluyao/Music";
  std::vector<std::string> filenames;
  for (const auto& entry : fs::directory_iterator(directoryPath)) {
    if (entry.is_regular_file()) {
      filenames.push_back(entry.path().filename().string());
    }
  }
  auto startTime = std::chrono::high_resolution_clock::now();
  for (const auto& filename : filenames) {
    checksumCalc.calculateHeaderSHA1(directoryPath + filename, 1);
  }
  auto endTime = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = endTime - startTime;
  qDebug() << "Execution time: " << duration.count() << " seconds";
}

QTEST_MAIN(TestChecksum)
#include "tst_checksum.moc"
