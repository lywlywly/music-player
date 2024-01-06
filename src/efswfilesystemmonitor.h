#ifndef EFSWFILESYSTEMMONITOR_H
#define EFSWFILESYSTEMMONITOR_H

#include <QObject>

#include "ifilesystemmonitor.h"
#include "include/efsw/efsw.hpp"

class EFSWFileSystemMonitor : public QObject,
                    public efsw::FileWatchListener,
                    virtual public IFileSystemMonitor {
  Q_OBJECT
  Q_INTERFACES(IFileSystemMonitor)
 public:
  explicit EFSWFileSystemMonitor(QObject* parent = nullptr);
  void handleFileAction(efsw::WatchID watchid, const std::string& dir,
                        const std::string& filename, efsw::Action action,
                        std::string oldFilename) override;
  void addWatchingPath(const QString& path) override;
 signals:
  void directoryChanged(const QString& path) override;
  void fileChanged(const QString& path) override;
};

#endif  // EFSWFILESYSTEMMONITOR_H
