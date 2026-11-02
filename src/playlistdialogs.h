#ifndef PLAYLISTDIALOGS_H
#define PLAYLISTDIALOGS_H

// UI flows that run one Playlist operation with the dialogs around it: a file
// picker, a warning, or a blocking progress dialog.

class Playlist;
class QWidget;

// Asks for audio files (starting in the Music folder) and adds them to
// playlist with Playlist::addFiles. Shows a warning only when the single
// selected file cannot be read; in a multi-file add, failed files are skipped
// silently.
void addFilesFromDialog(QWidget *parent, Playlist &playlist);

// Asks for a folder and adds the audio files directly in it (not in
// subfolders) to playlist, with a modal progress dialog. Failed files are
// skipped silently.
void addFolderFromDialog(QWidget *parent, Playlist &playlist);

// Re-parses the metadata of every song in playlist
// (Playlist::refreshMetadataFromFiles), with a modal progress dialog.
void refreshMetadataWithProgress(QWidget *parent, Playlist &playlist);

#endif // PLAYLISTDIALOGS_H
