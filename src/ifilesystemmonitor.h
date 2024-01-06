#ifndef IFILESYSTEMMONITOR_H
#define IFILESYSTEMMONITOR_H

#include <QObject>

class IFileSystemMonitor {
 public:
  // explicit IFileSystemMonitor(QObject *parent = nullptr);
  virtual void addWatchingPath(const QString &path) = 0;

 signals:
  virtual void directoryChanged(const QString &path) = 0;
  virtual void fileChanged(const QString &path) = 0;
};
Q_DECLARE_INTERFACE(IFileSystemMonitor, "IMyInterfaces");
#endif  // IFILESYSTEMMONITOR_H
