#ifndef LIBRARYFOLDERSPAGE_H
#define LIBRARYFOLDERSPAGE_H

#include <QWidget>

class MonitoredLibrary;
class QLabel;
class QListWidget;
class QPushButton;

// The Settings > Library page: the monitored library's watched folders, with
// Add…, Remove, Rescan now, and Remove missing songs for a folder that is
// offline because its root is empty. Actions apply at once, not on the
// dialog's OK. Shows scan progress.
class LibraryFoldersPage : public QWidget {
  Q_OBJECT

public:
  explicit LibraryFoldersPage(MonitoredLibrary &monitoredLibrary,
                              QWidget *parent = nullptr);

  // Watches path, or shows MonitoredLibrary::addFolder's error. Add… calls it
  // with the chosen directory.
  void addFolder(const QString &path);

private:
  void refreshFolders();
  void updateButtons();
  int selectedFolderId() const;

  MonitoredLibrary &monitoredLibrary_;
  QListWidget *foldersList_ = nullptr;
  QPushButton *removeButton_ = nullptr;
  QPushButton *rescanButton_ = nullptr;
  QPushButton *removeMissingButton_ = nullptr;
  QLabel *scanStatusLabel_ = nullptr;
};

#endif // LIBRARYFOLDERSPAGE_H
