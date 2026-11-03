#ifndef AUDIOFILEFORMATS_H
#define AUDIOFILEFORMATS_H

#include <QString>
#include <QStringList>
#include <QStringView>

// The audio file extensions the app picks up: Open, Add Folder and the
// monitored library scan.

// Lower-case suffixes, without the dot.
inline const QStringList &audioFileSuffixes() {
  static const QStringList suffixes = {"mp3", "flac", "m4a", "wav",
                                       "ogg", "opus", "alac"};
  return suffixes;
}

// Name filters ("*.mp3", ...) for QDir and file dialogs.
inline const QStringList &audioFileNameFilters() {
  static const QStringList filters = [] {
    QStringList result;
    for (const QString &suffix : audioFileSuffixes()) {
      result.push_back(QStringLiteral("*.") + suffix);
    }
    return result;
  }();
  return filters;
}

// Whether fileName ends in a supported audio suffix, compared
// case-insensitively.
inline bool hasAudioFileSuffix(QStringView fileName) {
  const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
  if (dot < 0) {
    return false;
  }
  const QStringView suffix = fileName.mid(dot + 1);
  for (const QString &known : audioFileSuffixes()) {
    if (suffix.compare(known, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

#endif // AUDIOFILEFORMATS_H
