#ifndef DUMMYFILESYSTEMMONITOR_H
#define DUMMYFILESYSTEMMONITOR_H

#include "ifilesystemmonitor.h"

// For tests: records watches; tests emit the signals themselves.
class DummyFileSystemMonitor : public IFileSystemMonitor {
public:
  using IFileSystemMonitor::IFileSystemMonitor;

  bool addPath(const QString &path) override {
    if (refusedPaths.contains(path)) {
      return false;
    }
    if (!watchedPaths.contains(path)) {
      watchedPaths.push_back(path);
    }
    return true;
  }
  bool removePath(const QString &path) override {
    return watchedPaths.removeAll(path) > 0;
  }

  // In the order they were added.
  QStringList watchedPaths;
  // addPath fails for these.
  QStringList refusedPaths;
};

#endif // DUMMYFILESYSTEMMONITOR_H
