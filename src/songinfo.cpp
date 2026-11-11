#include "songinfo.h"

#include "columnregistry.h"
#include "monitoredlibrary.h"
#include "songlibrary.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>

namespace {

// The format of the table's date columns (EpochSecondsDateTime).
QString formatDateTime(const QDateTime &dateTime) {
  return dateTime.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString columnTitle(const ColumnRegistry &registry, const char *fieldId) {
  const QString id = QString::fromLatin1(fieldId);
  const ColumnDefinition *definition = registry.findColumn(id);
  return definition != nullptr ? definition->title : id;
}

// Appends a row for the stored field, unless the song has no value for it.
void addFieldRow(SongInfoSection &section, const MSong &song,
                 const ColumnRegistry &registry, const char *fieldId,
                 const QString &unit = {}) {
  const auto it = song.find(fieldId);
  if (it == song.end()) {
    return;
  }
  const QString value = it->second.display();
  if (value.isEmpty()) {
    return;
  }
  section.rows.push_back(
      {.label = columnTitle(registry, fieldId),
       .value = unit.isEmpty() ? value : value + QLatin1Char(' ') + unit});
}

void addRow(SongInfoSection &section, const QString &label,
            const QString &value, const QString &toolTip = {}) {
  section.rows.push_back({.label = label, .value = value, .toolTip = toolTip});
}

} // namespace

SongInfo buildSongInfo(int songPk, const SongLibrary &library,
                       const MonitoredLibrary &monitoredLibrary,
                       const ColumnRegistry &registry) {
  if (songPk < 0) {
    return {};
  }
  const MSong &song = library.getSongByPK(songPk);
  const auto pathIt = song.find("filepath");
  if (pathIt == song.end() || pathIt->second.text.empty()) {
    return {};
  }

  SongInfo info;
  info.songPk = songPk;
  const QString storedPath = QString::fromStdString(pathIt->second.text);
  const std::optional<MonitoredLibrary::Folder> folder =
      monitoredLibrary.folderOf(songPk);

  // QFileInfo splits the path without touching the file; exists() and the
  // stats below read it.
  QFileInfo file(storedPath);
  SongInfoSection fileSection{.title = QObject::tr("File")};
  addRow(fileSection, QObject::tr("File name"), file.fileName());
  addRow(fileSection, QObject::tr("Folder"),
         QDir::toNativeSeparators(file.absolutePath()));
  addRow(fileSection, QObject::tr("Path"),
         QDir::toNativeSeparators(storedPath));
  // A stat on an unmounted (network) drive can block.
  if (folder.has_value() && !folder->online) {
    addRow(fileSection, QObject::tr("Status"), QObject::tr("Folder offline"));
  } else if (!file.exists()) {
    addRow(fileSection, QObject::tr("Status"), QObject::tr("File not found"));
  } else {
    const QLocale locale;
    addRow(fileSection, QObject::tr("Size"),
           locale.formattedDataSize(file.size()),
           QObject::tr("%1 bytes").arg(locale.toString(file.size())));
    addRow(fileSection, QObject::tr("Modified"),
           formatDateTime(file.lastModified()));
    const QDateTime created = file.birthTime();
    addRow(fileSection, QObject::tr("Created"),
           created.isValid() ? formatDateTime(created)
                             : QObject::tr("Unknown"));
  }
  info.sections.push_back(std::move(fileSection));

  SongInfoSection audio{.title = QObject::tr("Audio")};
  addFieldRow(audio, song, registry, "codec");
  addFieldRow(audio, song, registry, "duration");
  // TagLib reports kb/s and Hz; the table shows the bare numbers.
  addFieldRow(audio, song, registry, "bitrate", QStringLiteral("kbps"));
  addFieldRow(audio, song, registry, "sample_rate", QStringLiteral("Hz"));
  addFieldRow(audio, song, registry, "channels");
  info.sections.push_back(std::move(audio));

  SongInfoSection tags{.title = QObject::tr("Tags")};
  for (const char *fieldId : {"title", "artist", "album", "discnumber",
                              "tracknumber", "date", "genre"}) {
    addFieldRow(tags, song, registry, fieldId);
  }
  info.sections.push_back(std::move(tags));

  SongInfoSection stats{.title = QObject::tr("Library")};
  addFieldRow(stats, song, registry, "play_count");
  addFieldRow(stats, song, registry, "last_played_timestamp");
  if (folder.has_value()) {
    const QString folderPath = QDir::toNativeSeparators(folder->path);
    addRow(stats, QObject::tr("Watched folder"),
           folder->online ? folderPath
                          : QObject::tr("%1 (offline)").arg(folderPath));
  }
  info.sections.push_back(std::move(stats));

  // Leave out the sections with no rows.
  std::erase_if(info.sections, [](const SongInfoSection &section) {
    return section.rows.empty();
  });
  return info;
}
