#include "efswfilesystemmonitor.h"

#include <QDebug>
#include <QDir>
#include <QMetaObject>
#include <efsw/efsw.hpp>

namespace {
// name is relative to dir, or absolute (efsw reports the old path of some
// moves in full).
QString joinPath(const std::string &dir, const std::string &name) {
  const QString path = QDir::fromNativeSeparators(QString::fromStdString(name));
  return QDir::cleanPath(
      QDir::isAbsolutePath(path)
          ? path
          : QDir::fromNativeSeparators(QString::fromStdString(dir)) +
                QLatin1Char('/') + path);
}
} // namespace

// Runs on the efsw thread; only queues signals to the monitor's thread.
class EfswFileSystemMonitor::Listener : public efsw::FileWatchListener {
public:
  explicit Listener(EfswFileSystemMonitor *monitor) : monitor_(monitor) {}

  void handleFileAction(efsw::WatchID, const std::string &dir,
                        const std::string &filename, efsw::Action action,
                        const std::string &oldFilename) override {
    QStringList paths{joinPath(dir, filename)};
    if (action == efsw::Actions::Moved && !oldFilename.empty()) {
      paths.push_back(joinPath(dir, oldFilename));
    }
    QMetaObject::invokeMethod(
        monitor_,
        [monitor = monitor_, paths]() { emit monitor->pathsChanged(paths); },
        Qt::QueuedConnection);
  }

  void handleMissedFileActions(efsw::WatchID, const std::string &dir) override {
    const QString path = joinPath(dir, {});
    QMetaObject::invokeMethod(
        monitor_,
        [monitor = monitor_, path]() { emit monitor->folderNeedsRescan(path); },
        Qt::QueuedConnection);
  }

private:
  // Queued calls to it are dropped once it is destroyed; the efsw thread has
  // stopped by then (see the member order).
  EfswFileSystemMonitor *monitor_;
};

EfswFileSystemMonitor::EfswFileSystemMonitor(QObject *parent)
    : IFileSystemMonitor(parent), listener_(std::make_unique<Listener>(this)),
      watcher_(std::make_unique<efsw::FileWatcher>()) {}

// watcher_ is destroyed first (reverse member order), which stops its thread
// before listener_ goes.
EfswFileSystemMonitor::~EfswFileSystemMonitor() = default;

bool EfswFileSystemMonitor::addPath(const QString &path) {
  if (watchIds_.contains(path)) {
    return true;
  }
  const efsw::WatchID id = watcher_->addWatch(
      QDir::toNativeSeparators(path).toStdString(), listener_.get(), true);
  if (id < 0) {
    qWarning() << "EfswFileSystemMonitor: cannot watch" << path << ":"
               << QString::fromStdString(efsw::Errors::Log::getLastErrorLog());
    return false;
  }
  watchIds_.insert(path, id);
  if (!started_) {
    watcher_->watch();
    started_ = true;
  }
  return true;
}

bool EfswFileSystemMonitor::removePath(const QString &path) {
  const auto it = watchIds_.constFind(path);
  if (it == watchIds_.cend()) {
    return false;
  }
  watcher_->removeWatch(it.value());
  watchIds_.erase(it);
  return true;
}
