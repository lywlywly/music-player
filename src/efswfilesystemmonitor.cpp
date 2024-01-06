#include "efswfilesystemmonitor.h"

#include <iostream>
EFSWFileSystemMonitor::EFSWFileSystemMonitor(QObject* parent)
    : QObject{parent} {}

void EFSWFileSystemMonitor::handleFileAction(efsw::WatchID watchid,
                                             const std::string& dir,
                                             const std::string& filename,
                                             efsw::Action action,
                                             std::string oldFilename) {
  switch (action) {
    case efsw::Actions::Add:
      std::cout << "DIR (" << dir << ") FILE (" << filename
                << ") has event Added" << std::endl;
      emit fileChanged(QString::fromStdString(filename));
      break;
    case efsw::Actions::Delete:
      std::cout << "DIR (" << dir << ") FILE (" << filename
                << ") has event Delete" << std::endl;
      emit fileChanged(QString::fromStdString(filename));
      break;
    case efsw::Actions::Modified:
      std::cout << "DIR (" << dir << ") FILE (" << filename
                << ") has event Modified" << std::endl;
      emit fileChanged(QString::fromStdString(filename));
      break;
    case efsw::Actions::Moved:
      std::cout << "DIR (" << dir << ") FILE (" << filename
                << ") has event Moved from (" << oldFilename << ")"
                << std::endl;
      emit fileChanged(QString::fromStdString(filename));
      break;
    default:
      std::cout << "Should never happen!" << std::endl;
  }
}

// TODO: allow multiple watchers
void EFSWFileSystemMonitor::addWatchingPath(const QString& path) {
  efsw::FileWatcher* fileWatcher = new efsw::FileWatcher();
  efsw::WatchID watchID =
      fileWatcher->addWatch("/home/luyao/Documents/test/", this, true);
  fileWatcher->watch();
}
