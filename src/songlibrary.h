#ifndef SONGLIBRARY_H
#define SONGLIBRARY_H

#include "columnregistry.h"
#include "fieldvalue.h"
#include <QSqlDatabase>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#ifdef MYPLAYER_TESTING
#include <functional>
#endif

using MSong = std::unordered_map<std::string, FieldValue>;
class DatabaseManager;
#ifdef MYPLAYER_TESTING
using SongParseFn =
    std::function<MSong(const std::string &, const ColumnRegistry &)>;
#endif

// SongLibrary is the canonical repository for song metadata in memory and in
// the database. Songs are indexed by song_id and filepath, with filepath as
// the unique identity. Normal single-song updates are synchronous; bulk loads
// and refreshes may take time; UI-first playlist clears update memory
// immediately and delete DB rows asynchronously; startup load and playlist
// restore stay synchronous so memory matches the database before use.
class SongLibrary {
public:
#ifdef MYPLAYER_TESTING
  explicit SongLibrary(const ColumnRegistry &columnRegistry,
                       DatabaseManager &databaseManager,
                       SongParseFn parseSong = {});
#else
  explicit SongLibrary(const ColumnRegistry &columnRegistry,
                       DatabaseManager &databaseManager);
#endif
  // add to library if not exists, else do nothing, return primary key
  int addTolibrary(MSong &&);
  const MSong &getSongByPK(int) const;
  std::vector<int> query(std::string) const;
  const std::vector<int> &registerQuery(std::string) const;
  void unregisterQuery(std::string) const;
  // no duplicate
  // std::unordered_set<const std::string*, StringPtrHash, StringPtrEqual>
  // queryField(std::string) const;
  std::unordered_set<std::string> queryField(std::string) const;
  const std::unordered_set<std::string> &registerQueryField(std::string) const;
  void unRegisterQueryField(std::string) const;
  // TODO: remove, by marking as deleted, also update registered queries
  // TODO: serialize, skipping songs marked deleted
  // TODO: deserialize
  // Loads the full song set (built-in columns + dynamic attributes) from DB
  // into memory. This can take noticeable time on large libraries.
  void loadFromDatabase();
  // Loads playlist state from DB table `playlists` and DB table
  // `playlist_items` for playlistId.
  bool loadPlaylistState(int playlistId, int &lastPlayed,
                         std::vector<int> &songIds);
  // Re-parses one song by filepath, updates DB + in-memory fields, and returns
  // the refreshed in-memory song.
  const MSong &refreshSongFromFile(const std::string &path);
  // Re-parses a filepath list (deduped internally), then updates DB and
  // in-memory fields for each song. This can take noticeable time on large
  // lists; progressCallback receives (current, total) after each refresh for
  // progress UI.
  void refreshSongsFromFilepaths(
      const std::vector<std::string> &filepaths,
      const std::function<void(int current, int total)> &progressCallback = {});
  // Appends one song to DB table `playlist_items` at the given playlist
  // position.
  void appendSongToPlaylistInDb(int playlistId, int songId, int position);
  // Removes all rows from DB table `playlist_items` for the given playlistId.
  // This does not delete any rows from DB table `songs`. For file-backed DBs
  // this is dispatched asynchronously so UI clear can stay immediate.
  void removePlaylistItemsInDb(int playlistId);

private:
  void loadBuiltInSongs();
  // Loads dynamic song attributes from DB table `song_attributes` into memory.
  // Attributes whose `key` no longer has a matching definition in
  // `attribute_definitions` are treated as stale: they are skipped in memory
  // and deleted from DB instead of aborting the load.
  void loadDynamicAttributes();
  int ensureSongInDb(const MSong &song);
  // Syncs built-in song fields in memory and DB for a known song_id.
  void syncBuiltInFieldsBySongId(int songId, const MSong &song);
  // Syncs dynamic attributes in memory and DB for a known song_id.
  void syncDynamicAttributesBySongId(int songId, const MSong &song);
  const ColumnRegistry &columnRegistry_;
  DatabaseManager &databaseManager_;
#ifdef MYPLAYER_TESTING
  SongParseFn parseSong_;
#endif
  std::vector<MSong> songs;
  std::unordered_map<std::string, int> paths;
  mutable std::unordered_map<std::string, std::unordered_set<std::string>>
      registeredQueryFields;
  mutable std::unordered_map<std::string, std::vector<int>> registeredQueries;
};

#endif // SONGLIBRARY_H
