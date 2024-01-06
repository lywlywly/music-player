#ifndef QFILESYSTEMMONITOR_H
#define QFILESYSTEMMONITOR_H

#include <QObject>

#include "ifilesystemmonitor.h"
#include "qfilesystemwatcher.h"

class QFileSystemMonitor : public QObject, virtual public IFileSystemMonitor {
  Q_OBJECT
  Q_INTERFACES(IFileSystemMonitor)
 public:
  explicit QFileSystemMonitor(QObject *parent = nullptr);
  void addWatchingPath(const QString &path) override;
 signals:
  void directoryChanged(const QString &path) override;
  void fileChanged(const QString &path) override;

 private:
  QFileSystemWatcher qFSWatcher;
};

#endif  // QFILESYSTEMMONITOR_H
