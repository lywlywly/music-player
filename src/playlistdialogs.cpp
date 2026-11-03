#include "playlistdialogs.h"

#include "audiofileformats.h"
#include "playlist.h"
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStandardPaths>
#include <functional>
#include <string>
#include <vector>

namespace {
// Runs work with a window-modal progress dialog (no cancel) over parent. The
// callback passed to work updates the dialog and processes events.
void runWithProgressDialog(
    QWidget *parent, const QString &label, int total,
    const std::function<void(const Playlist::ProgressFn &)> &work) {
  QProgressDialog progressDialog(label, QString{}, 0, total, parent);
  progressDialog.setWindowModality(Qt::WindowModal);
  progressDialog.setCancelButton(nullptr);
  progressDialog.setMinimumDuration(0);
  progressDialog.setValue(0);
  work([&](int current, int reportedTotal) {
    progressDialog.setMaximum(reportedTotal);
    progressDialog.setValue(current);
    QApplication::processEvents();
  });
  progressDialog.setValue(progressDialog.maximum());
}
} // namespace

void addFilesFromDialog(QWidget *parent, Playlist &playlist) {
  const QList<QString> fileName = QFileDialog::getOpenFileNames(
      parent, "Open the file",
      QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
      QStringLiteral("Audio Files (%1);;All Files (*.*)")
          .arg(audioFileNameFilters().join(QLatin1Char(' '))));
  std::vector<std::string> paths;
  paths.reserve(static_cast<size_t>(fileName.size()));
  for (const QString &s : fileName) {
    paths.push_back(s.toStdString());
  }
  std::vector<std::string> failedPaths;
  playlist.addFiles(paths, {}, &failedPaths);
  if (paths.size() == 1 && !failedPaths.empty()) {
    QMessageBox::warning(parent, QObject::tr("Add File"),
                         QObject::tr("Could not read the file:\n%1")
                             .arg(QDir::toNativeSeparators(
                                 QString::fromStdString(failedPaths.front()))));
  }
}

void addFolderFromDialog(QWidget *parent, Playlist &playlist) {
  QString dirPath = QFileDialog::getExistingDirectory(
      parent, "Select Folder",
      QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
      QFileDialog::ShowDirsOnly);

  if (dirPath.isEmpty())
    return;

  QDir dir(dirPath);
  const QStringList files = dir.entryList(audioFileNameFilters(), QDir::Files);

  std::vector<std::string> paths;
  paths.reserve(static_cast<size_t>(files.size()));
  for (const QString &s : files) {
    paths.push_back(dir.absoluteFilePath(s).toStdString());
  }
  runWithProgressDialog(parent, "Loading songs from folder...",
                        static_cast<int>(paths.size()),
                        [&](const Playlist::ProgressFn &progress) {
                          playlist.addFiles(paths, progress);
                        });
}

void refreshMetadataWithProgress(QWidget *parent, Playlist &playlist) {
  runWithProgressDialog(parent, "Refreshing playlist metadata...",
                        playlist.songCount(),
                        [&](const Playlist::ProgressFn &progress) {
                          playlist.refreshMetadataFromFiles(progress);
                        });
}
