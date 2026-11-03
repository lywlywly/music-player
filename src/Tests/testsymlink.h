#ifndef TESTSYMLINK_H
#define TESTSYMLINK_H

#include <QFileInfo>
#include <QString>
#include <QTest>
#include <QtGlobal>
#include <filesystem>
#include <string>
#include <system_error>

// Creates a real symbolic link at link pointing to target (a file or a
// directory); QFile::link makes a .lnk shortcut on Windows, a plain file.
// Returns false, with a warning, where Windows refuses symlinks (no Developer
// Mode or admin rights), and callers then skip their symlink checks. Any other
// failure fails the current test.
inline bool makeSymlink(const QString &target, const QString &link) {
  const std::filesystem::path targetPath =
      QFileInfo(target).filesystemFilePath();
  const std::filesystem::path linkPath = QFileInfo(link).filesystemFilePath();
  std::error_code ec;
  if (QFileInfo(target).isDir()) {
    std::filesystem::create_directory_symlink(targetPath, linkPath, ec);
  } else {
    std::filesystem::create_symlink(targetPath, linkPath, ec);
  }
  if (!ec) {
    return true;
  }
#ifdef Q_OS_WIN
  constexpr int kErrorPrivilegeNotHeld = 1314; // ERROR_PRIVILEGE_NOT_HELD
  if (ec == std::error_code(kErrorPrivilegeNotHeld, std::system_category())) {
    qWarning("makeSymlink: symlinks not permitted; symlink checks skipped");
    return false;
  }
#endif
  const std::string message = "makeSymlink failed: " + ec.message();
  QTest::qFail(message.c_str(), __FILE__, __LINE__);
  return false;
}

#endif // TESTSYMLINK_H
