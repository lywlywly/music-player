#include "songdialogs.h"

#include "filemanagerutils.h"
#include "monitoredlibrary.h"
#include "playlistcollection.h"
#include "songpropertiesdialog.h"
#include <QFileInfo>
#include <QMessageBox>
#include <string>
#include <unordered_map>

void openSongProperties(QWidget *parent, int songPk, SongLibrary &library,
                        const ColumnRegistry &registry,
                        PlaylistCollection &collection) {
  const MSong &song = library.getSongByPK(songPk);
  const auto filepathIt = song.find("filepath");
  if (filepathIt == song.end() || filepathIt->second.text.empty()) {
    return;
  }
  // A copy: the refresh replaces the song's fields.
  const std::string filepath = filepathIt->second.text;

  std::unordered_map<std::string, std::string> remainingFields;
  const MSong &refreshedSong =
      library.refreshSongFromFile(filepath, &remainingFields);

  auto *dialog =
      new SongPropertiesDialog(songPk, filepath, library, refreshedSong,
                               remainingFields, registry, parent);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setModal(true);
  // Store the file's new stats first, so the scan the write triggers finds
  // nothing to re-parse.
  QObject::connect(dialog, &SongPropertiesDialog::songUpdated,
                   &collection.monitoredLibrary(),
                   &MonitoredLibrary::noteSongWritten);
  QObject::connect(dialog, &SongPropertiesDialog::songUpdated, &collection,
                   &PlaylistCollection::notifySongChanged);
  dialog->show();
}

void revealSongFile(QWidget *parent, const MSong &song) {
  const QString title = QStringLiteral("Open Containing Folder");
  const auto filepathIt = song.find("filepath");
  const QString filepath =
      filepathIt == song.end()
          ? QString()
          : QString::fromStdString(filepathIt->second.text).trimmed();
  const QFileInfo fileInfo(filepath);
  if (filepath.isEmpty() || !fileInfo.exists() || !fileInfo.isFile()) {
    QMessageBox::warning(
        parent, title,
        QStringLiteral("The selected file does not exist:\n%1").arg(filepath));
    return;
  }

  if (!FileManagerUtils::revealFile(fileInfo.absoluteFilePath())) {
    QMessageBox::warning(
        parent, title,
        QStringLiteral("Could not open the containing folder for:\n%1")
            .arg(fileInfo.absoluteFilePath()));
  }
}
