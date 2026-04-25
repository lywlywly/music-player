#ifndef SONGSTORE_H
#define SONGSTORE_H

#include "songlibrary.h"

// Per-playlist store of song IDs and ordering. It provides index<->song_id
// mapping, sorting for the playlist view, and playlist-item loading from DB.
// Canonical song metadata lives in SongLibrary.
class SongStore {
public:
  // playlistId is expected to be > 0.
  SongStore(SongLibrary &, int = -1);
  int songCount() const;
  // Assumes DB write preconditions are satisfied; on violation/write failure,
  // process crashes via qFatal.
  void addSong(MSong &&);
  void addSongByPk(int songPk);
  // remove song by Pk
  void removeSongByPk(int);
  // remove song by index (current order)
  void removeSongByIndex(int);
  // Clears this playlist view immediately in memory. For persistent playlists
  // (playlistId > 0), DB table `playlist_items` deletion is enqueued after the
  // memory clear and is eventually consistent for file-backed DBs. Song rows in
  // DB table `songs` are not deleted.
  void clear();
  const MSong &getSongByPk(int) const;
  const MSong &getSongByIndex(int) const;
  int getPkByIndex(int) const;
  int getIndexByPk(int) const;
  void sortByField(std::string, int = 0);
  std::vector<int> expression(std::string);
  std::vector<int> &expressionView(std::string);
  std::vector<const std::string *> expressionField(std::string);
  const std::vector<int> &getSongsView() const;
  const std::vector<int> &getIndices() const;
  void refreshSongsFromFilepaths(
      const std::vector<std::string> &filepaths,
      const std::function<void(int current, int total)> &progressCallback = {});
  bool loadPlaylistState(int &lastPlayed);

private:
  void rebuildIndices();
  // Valid playlist ids are > 0.
  int playlistId;
  SongLibrary &library;
  std::vector<int> songPKs;
  std::vector<int> indices;
};

#endif // SONGSTORE_H
