#ifndef EFSWFILESYSTEMMONITOR_H
#define EFSWFILESYSTEMMONITOR_H

#include "ifilesystemmonitor.h"
#include <QHash>
#include <memory>

namespace efsw {
class FileWatcher;
}

// IFileSystemMonitor backed by efsw (one watcher thread for all watches).
// Events from that thread are forwarded with queued signals.
class EfswFileSystemMonitor : public IFileSystemMonitor {
  Q_OBJECT
public:
  explicit EfswFileSystemMonitor(QObject *parent = nullptr);
  ~EfswFileSystemMonitor() override;

  bool addPath(const QString &path) override;
  bool removePath(const QString &path) override;

private:
  class Listener;

  std::unique_ptr<Listener> listener_;
  std::unique_ptr<efsw::FileWatcher> watcher_;
  QHash<QString, long> watchIds_;
  bool started_ = false;
};

#endif // EFSWFILESYSTEMMONITOR_H
