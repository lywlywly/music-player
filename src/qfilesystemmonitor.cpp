#include "qfilesystemmonitor.h"

#include "qdebug.h"

QFileSystemMonitor::QFileSystemMonitor(QObject *parent) : QObject{parent} {
  connect(&qFSWatcher, SIGNAL(fileChanged(const QString &)),
          dynamic_cast<QObject *>(this), SIGNAL(fileChanged(const QString &)));
  connect(&qFSWatcher, SIGNAL(directoryChanged(const QString &)),
          dynamic_cast<QObject *>(this),
          SIGNAL(directoryChanged(const QString &)));
}

void QFileSystemMonitor::addWatchingPath(const QString &path) {
  qFSWatcher.addPath(path);
}
