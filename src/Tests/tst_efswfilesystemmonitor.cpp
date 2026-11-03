#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "../efswfilesystemmonitor.h"

namespace {
QStringList allPaths(const QSignalSpy &spy) {
  QStringList paths;
  for (const QList<QVariant> &args : spy) {
    paths += args.at(0).toStringList();
  }
  return paths;
}
} // namespace

// Uses the real file system and the platform's native watcher.
class TestEfswFileSystemMonitor : public QObject {
  Q_OBJECT

private slots:
  void watch_reportsChangesInSubfolders();
  void addPath_failsForMissingFolder();
};

void TestEfswFileSystemMonitor::watch_reportsChangesInSubfolders() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString root = QFileInfo(dir.path()).canonicalFilePath();
  QVERIFY(QDir().mkpath(root + "/sub"));
  EfswFileSystemMonitor monitor;
  QSignalSpy spy(&monitor, &IFileSystemMonitor::pathsChanged);
  QVERIFY(monitor.addPath(root));

  QFile file(root + "/sub/a.mp3");
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.close();
  QTRY_VERIFY_WITH_TIMEOUT(allPaths(spy).contains(root + "/sub/a.mp3"), 10000);

  // A move reports both paths.
  spy.clear();
  QVERIFY(QFile::rename(root + "/sub/a.mp3", root + "/b.mp3"));
  QTRY_VERIFY_WITH_TIMEOUT(allPaths(spy).contains(root + "/sub/a.mp3") &&
                               allPaths(spy).contains(root + "/b.mp3"),
                           10000);

  QVERIFY(monitor.removePath(root));
  QVERIFY(!monitor.removePath(root));
}

void TestEfswFileSystemMonitor::addPath_failsForMissingFolder() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  EfswFileSystemMonitor monitor;
  QTest::ignoreMessage(
      QtWarningMsg, QRegularExpression("EfswFileSystemMonitor: cannot watch"));
  QVERIFY(!monitor.addPath(dir.path() + "/missing"));
  QVERIFY(!monitor.removePath(dir.path() + "/missing"));
}

QTEST_GUILESS_MAIN(TestEfswFileSystemMonitor)
#include "tst_efswfilesystemmonitor.moc"
