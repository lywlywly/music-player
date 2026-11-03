#include "libraryfolderspage.h"

#include "monitoredlibrary.h"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {
constexpr int kFolderIdRole = Qt::UserRole;
constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kOfflineRole = Qt::UserRole + 2;
} // namespace

LibraryFoldersPage::LibraryFoldersPage(MonitoredLibrary &monitoredLibrary,
                                       QWidget *parent)
    : QWidget(parent), monitoredLibrary_(monitoredLibrary) {
  auto *note = new QLabel(
      tr("Audio files in these folders and their subfolders are kept in the "
         "library. Changes here apply immediately."),
      this);
  note->setWordWrap(true);
  foldersList_ = new QListWidget(this);
  foldersList_->setObjectName("library_folders_list");
  auto *addButton = new QPushButton(tr("Add..."), this);
  removeButton_ = new QPushButton(tr("Remove"), this);
  removeButton_->setObjectName("remove_library_folder_button");
  rescanButton_ = new QPushButton(tr("Rescan Now"), this);
  rescanButton_->setObjectName("rescan_library_button");
  removeMissingButton_ = new QPushButton(tr("Remove Missing Songs"), this);
  removeMissingButton_->setObjectName("remove_missing_songs_button");
  removeMissingButton_->setToolTip(
      tr("The folder is empty, so it is treated as an unmounted drive. Remove "
         "its songs from the library if the files were deleted."));
  scanStatusLabel_ = new QLabel(this);
  scanStatusLabel_->setObjectName("library_scan_status_label");

  auto *buttons = new QHBoxLayout();
  buttons->addWidget(addButton);
  buttons->addWidget(removeButton_);
  buttons->addWidget(rescanButton_);
  buttons->addWidget(removeMissingButton_);
  buttons->addStretch();
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(note);
  layout->addWidget(foldersList_);
  layout->addLayout(buttons);
  layout->addWidget(scanStatusLabel_);

  connect(addButton, &QPushButton::clicked, this, [this]() {
    const QString path = QFileDialog::getExistingDirectory(
        this, tr("Add Library Folder"),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    if (!path.isEmpty()) {
      addFolder(path);
    }
  });
  connect(removeButton_, &QPushButton::clicked, this, [this]() {
    monitoredLibrary_.removeFolder(selectedFolderId());
    refreshFolders();
  });
  connect(rescanButton_, &QPushButton::clicked, &monitoredLibrary_,
          &MonitoredLibrary::rescan);
  connect(removeMissingButton_, &QPushButton::clicked, this, [this]() {
    monitoredLibrary_.removeMissingSongs(selectedFolderId());
  });
  connect(foldersList_, &QListWidget::currentRowChanged, this,
          &LibraryFoldersPage::updateButtons);
  connect(&monitoredLibrary_, &MonitoredLibrary::folderAvailabilityChanged,
          this, &LibraryFoldersPage::refreshFolders);
  connect(&monitoredLibrary_, &MonitoredLibrary::scanProgress, this,
          [this](int done, int total) {
            scanStatusLabel_->setText(
                tr("Scanning library... %1 / %2").arg(done).arg(total));
          });
  connect(&monitoredLibrary_, &MonitoredLibrary::scanFinished, scanStatusLabel_,
          &QLabel::clear);
  if (monitoredLibrary_.isScanning()) {
    scanStatusLabel_->setText(tr("Scanning library..."));
  }
  refreshFolders();
}

void LibraryFoldersPage::addFolder(const QString &path) {
  const MonitoredLibrary::AddFolderResult result =
      monitoredLibrary_.addFolder(path);
  if (result.folderId < 0) {
    QMessageBox::warning(this, tr("Add Library Folder"), result.error);
    return;
  }
  refreshFolders();
}

void LibraryFoldersPage::refreshFolders() {
  const int selectedId = selectedFolderId();
  foldersList_->clear();
  for (const MonitoredLibrary::Folder &folder : monitoredLibrary_.folders()) {
    const QString path = QDir::toNativeSeparators(folder.path);
    auto *item =
        new QListWidgetItem(!folder.online ? tr("%1 (Offline)").arg(path)
                            : folder.liveUpdatesUnavailable
                                ? tr("%1 (Live updates unavailable)").arg(path)
                                : path,
                            foldersList_);
    item->setData(kFolderIdRole, folder.id);
    item->setData(kPathRole, folder.path);
    item->setData(kOfflineRole, !folder.online);
    if (folder.id == selectedId) {
      foldersList_->setCurrentItem(item);
    }
  }
  updateButtons();
}

void LibraryFoldersPage::updateButtons() {
  const QListWidgetItem *item = foldersList_->currentItem();
  removeButton_->setEnabled(item != nullptr);
  rescanButton_->setEnabled(foldersList_->count() > 0);
  // Offline although the root exists: offline because the root is empty.
  removeMissingButton_->setEnabled(
      item != nullptr && item->data(kOfflineRole).toBool() &&
      QFileInfo(item->data(kPathRole).toString()).isDir());
}

int LibraryFoldersPage::selectedFolderId() const {
  const QListWidgetItem *item = foldersList_->currentItem();
  return item == nullptr ? -1 : item->data(kFolderIdRole).toInt();
}
