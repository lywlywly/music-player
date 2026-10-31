#ifndef PLAYLIST_H
#define PLAYLIST_H

#include "songlibrary.h"
#include <QObject>
#include <functional>
class DatabaseManager;

// One playlist: ordered song membership, persistence (`playlist_items`,
// `playlists.last_played`), library ingest/refresh for its operations, and
// change signals. Backend class: no Qt view types, no playback status.
// Canonical song metadata lives in SongLibrary.
// playlistId <= 0 is non-persistent (no DB reads or writes).
class Playlist : public QObject {
  Q_OBJECT
public:
  using ProgressFn = std::function<void(int current, int total)>;

  Playlist(int playlistId, SongLibrary &, DatabaseManager &,
           QObject *parent = nullptr);
  // Loads last_played and playlist_items (by position). Emits no signals.
  // Returns false when the playlists row is missing or a query fails.
  bool loadState();

  int id() const;
  int songCount() const;
  bool empty() const;
  bool containsPk(int songPk) const;
  const MSong &getSongByPk(int) const;
  const MSong &getSongByIndex(int) const;
  int getIndexByPk(int) const;
  int getPkByIndex(int) const;
  int getLastPlayed() const;

  // Parses each path (SongLibrary::loadSongFromFile), ingests it
  // (addTolibrary), then appends one row per path. progress is called after
  // each path is ingested. Returns pks in input order.
  std::vector<int> addFiles(const std::vector<std::string> &paths,
                            const ProgressFn &progress = {});
  // Ingests a pre-parsed song (addTolibrary) and appends it. Assumes DB write
  // preconditions are satisfied; on violation/write failure, process crashes
  // via qFatal.
  void addSong(MSong &&);
  // Appends a song already in SongLibrary.
  void addSongByPk(int songPk);
  // Removes one row (current order) in memory only.
  void removeSong(int row);
  // Clears this playlist immediately in memory. For persistent playlists, DB
  // table `playlist_items` deletion runs in background for file-backed DBs
  // (sync for in-memory DB). Song rows in DB table `songs` are not deleted.
  void clear();
  // order: 0 ascending, otherwise descending.
  void sortByField(const std::string &fieldId, int order = 0);
  // Re-parses metadata for all songs currently in this playlist and writes the
  // refreshed values back to SongLibrary. progress is called after each
  // processed song with (current, total). Assumes every row has a valid
  // non-empty `filepath`; missing/empty filepath is treated as fatal.
  void refreshMetadataFromFiles(const ProgressFn &progress = {});
  void setLastPlayed(int songPk);
  // Emits songDataChanged(row) when songPk >= 0 and is in this playlist.
  // Does not mutate song data.
  void emitSongDataChangedBySongPk(int songPk);

signals:
  void aboutToInsertSongs(int first, int last);
  void songsInserted(int first, int last);
  void aboutToRemoveSongs(int first, int last);
  void songsRemoved(int first, int last);
  void aboutToResetSongs();
  void songsReset();
  void songsReordered();
  void songDataChanged(int row);

private:
  void appendSongPk(int songId);
  void appendSongToPlaylistInDb(int songId, int position);
  void removePlaylistItemsInDb();
  void rebuildIndices();
  int playlistId_;
  SongLibrary &library_;
  DatabaseManager &databaseManager_;
  std::vector<int> songPKs_;
  std::vector<int> indices_;
  int lastPlayed_ = 1;
};

#endif // PLAYLIST_H
