#ifndef SONGDIALOGS_H
#define SONGDIALOGS_H

// UI flows for one song: the Song Properties dialog, and revealing its file
// with a warning when that fails.

#include "songlibrary.h"

class ColumnRegistry;
class PlaylistCollection;
class QWidget;

// Re-parses the song from its file (SongLibrary::refreshSongFromFile) and shows
// a modal SongPropertiesDialog for it. A saved edit stores the file's new
// stats in the monitored library (MonitoredLibrary::noteSongWritten) and
// refreshes the song's rows in every playlist
// (PlaylistCollection::notifySongChanged). Does nothing when the song has no
// file path.
void openSongProperties(QWidget *parent, int songPk, SongLibrary &library,
                        const ColumnRegistry &registry,
                        PlaylistCollection &collection);

// Shows song's file in the platform file manager. Warns when the file does not
// exist or the file manager cannot be opened.
void revealSongFile(QWidget *parent, const MSong &song);

#endif // SONGDIALOGS_H
