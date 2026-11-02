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
// A song appears at most once; adding a song already present is a no-op.
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
  // (addTolibrary), then appends one row per song not already in the
  // playlist. Paths that fail to parse are skipped and, when failedPaths is
  // provided, appended to it in input order. progress is called after each
  // path is processed. Returns the appended pks in input order.
  std::vector<int> addFiles(const std::vector<std::string> &paths,
                            const ProgressFn &progress = {},
                            std::vector<std::string> *failedPaths = nullptr);
  // Ingests a pre-parsed song (addTolibrary) and appends it if not already in
  // the playlist. Returns whether it was appended. Assumes DB write
  // preconditions are satisfied; on violation/write failure, process crashes
  // via qFatal.
  bool addSong(MSong &&);
  // Appends a song already in SongLibrary if not already in the playlist.
  // Returns whether it was appended.
  bool addSongByPk(int songPk);
  // Removes the rows (current order; duplicates ignored) and their
  // `playlist_items` rows, then emits songPksRemoved. Song rows in DB table
  // `songs` are not deleted here; see PlaylistCollection.
  void removeSongs(std::vector<int> rows);
  // Clears this playlist and its `playlist_items` rows, then emits
  // songPksRemoved if it was not empty. Song rows in DB table `songs` are not
  // deleted here; see PlaylistCollection.
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
  // After removeSongs or clear: the pks of the songs that left the playlist.
  void songPksRemoved(const std::vector<int> &songPks);
  void aboutToResetSongs();
  void songsReset();
  void songsReordered();
  void songDataChanged(int row);

private:
  void appendSongPk(int songId);
  void appendSongToPlaylistInDb(int songId, int position);
  void removePlaylistItemInDb(int songId);
  void removePlaylistItemsInDb();
  void rebuildIndices();
  int playlistId_;
  SongLibrary &library_;
  DatabaseManager &databaseManager_;
  std::vector<int> songPKs_;
  std::vector<int> indices_;
  int lastPlayed_ = 1;
  // Position for the next appended `playlist_items` row; above every stored
  // position, so removals cannot cause collisions.
  int nextPosition_ = 1;
};

#endif // PLAYLIST_H
