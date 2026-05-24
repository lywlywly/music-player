#include "filemanagerutils.h"

#include <QDesktopServices>
#include <QDir>
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#endif
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QUrl>
#include <QtGlobal>

namespace FileManagerUtils {

bool revealFile(const QString &filePath) {
#ifdef Q_OS_MACOS
  return QProcess::startDetached(QStringLiteral("open"),
                                 {QStringLiteral("-R"), filePath});
#elif defined(Q_OS_WIN)
  const QString nativePath = QDir::toNativeSeparators(filePath);
  return QProcess::startDetached(QStringLiteral("explorer.exe"),
                                 {QStringLiteral("/select,") + nativePath});
#elif defined(Q_OS_LINUX)
  QDBusInterface fileManager(QStringLiteral("org.freedesktop.FileManager1"),
                             QStringLiteral("/org/freedesktop/FileManager1"),
                             QStringLiteral("org.freedesktop.FileManager1"),
                             QDBusConnection::sessionBus());
  if (fileManager.isValid()) {
    const QDBusMessage reply = fileManager.call(
        QStringLiteral("ShowItems"),
        QStringList{QUrl::fromLocalFile(filePath).toString()}, QString());
    if (reply.type() != QDBusMessage::ErrorMessage) {
      return true;
    }
  }

  const QFileInfo fileInfo(filePath);
  return QDesktopServices::openUrl(
      QUrl::fromLocalFile(fileInfo.absolutePath()));
#else
  const QFileInfo fileInfo(filePath);
  return QDesktopServices::openUrl(
      QUrl::fromLocalFile(fileInfo.absolutePath()));
#endif
}

} // namespace FileManagerUtils
