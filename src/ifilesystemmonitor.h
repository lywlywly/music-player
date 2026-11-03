#ifndef IFILESYSTEMMONITOR_H
#define IFILESYSTEMMONITOR_H

#include <QObject>
#include <QString>
#include <QStringList>

// Recursive directory watches. Events are only hints that paths changed; the
// receiver scans them to find out what happened. Signals are always delivered
// on the thread the monitor lives in.
class IFileSystemMonitor : public QObject {
  Q_OBJECT
public:
  explicit IFileSystemMonitor(QObject *parent = nullptr) : QObject(parent) {}
  ~IFileSystemMonitor() override = default;

  // Watches the directory path and its subdirectories. Returns whether the
  // watch was added.
  virtual bool addPath(const QString &path) = 0;
  // Returns whether path was watched.
  virtual bool removePath(const QString &path) = 0;

signals:
  // Absolute paths of files or directories that were added, changed, removed,
  // or moved (both the old and the new path).
  void pathsChanged(const QStringList &paths);
  // Events under the watched directory path were lost; scan all of it.
  void folderNeedsRescan(const QString &path);
};

#endif // IFILESYSTEMMONITOR_H
