#ifndef SONGLIBRARY_H
#define SONGLIBRARY_H

#include "columnregistry.h"
#include "fieldvalue.h"
#include "libraryexpression.h"
#include <QSqlDatabase>
#include <QString>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using MSong = std::unordered_map<std::string, FieldValue>;

inline QString songFieldText(const MSong &song, const std::string &field) {
  auto it = song.find(field);
  if (it == song.end()) {
    return QStringLiteral("");
  }
  return QString::fromStdString(it->second.text);
}

class DatabaseManager;
#ifdef MYPLAYER_TESTING
using SongParseFn =
    std::function<MSong(const std::string &, const ColumnRegistry &,
                        std::unordered_map<std::string, std::string> *)>;
#endif

// SongLibrary is the canonical repository for song metadata in memory and in
// the database. Prefer song_id (pk) for cross-component references; filepath
// is mainly used for dedup and parser refresh lookup. Songs are indexed by
// song_id and filepath in memory; play statistics are keyed by song identity
// (normalized title|artist|album) in DB. Multiple songs can share the same
// identity. Playlist metadata and playlist_items persistence are handled
// outside SongLibrary.
// Normal single-song updates are synchronous; bulk loads and refreshes may take
// time.
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
  // Adds a fully prepared song map to library/DB and returns primary key.
  // If filepath already exists, syncs DB and in-memory fields from prepared
  // song instead of creating a new row.
  int addTolibrary(MSong &&);
  const MSong &getSongByPK(int) const;
  ExprParseResult parseLibraryExpression(const QString &expressionText) const;
  std::vector<int> search(const Expr &expression) const;
  std::vector<int> query(std::string) const;
  const std::vector<int> &registerQuery(std::string) const;
  void unregisterQuery(std::string) const;
  std::unordered_set<std::string> queryField(std::string) const;
  const std::unordered_set<std::string> &registerQueryField(std::string) const;
  void unRegisterQueryField(std::string) const;
  // TODO: remove, by marking as deleted, also update registered queries
  // TODO: serialize, skipping songs marked deleted
  // TODO: deserialize
  // Loads the full song set (built-in columns + dynamic attributes) from DB
  // into memory. This can take noticeable time on large libraries.
  void loadFromDatabase();
  // Re-parses one song by filepath, updates DB + in-memory fields, and returns
  // the refreshed in-memory song. When remainingFields is provided, parser
  // leftovers (unbound tags) are written there for caller-side use.
  const MSong &refreshSongFromFile(
      const std::string &path,
      std::unordered_map<std::string, std::string> *remainingFields = nullptr);
  // Parses one file and returns a prepared song map containing:
  // built-in fields, dynamic attrs from parser, computed fields, and
  // song_identity_key.
  MSong loadSongFromFile(const std::string &path) const;
  // Re-parses a filepath list (deduped internally), then updates DB and
  // in-memory fields for each song. This can take noticeable time on large
  // lists; progressCallback receives (current, total) after each refresh for
  // progress UI.
  void refreshSongsFromFilepaths(
      const std::vector<std::string> &filepaths,
      const std::function<void(int current, int total)> &progressCallback = {});
  // Updates last_played_timestamp for the identity of songPk.
  // This also updates all in-memory songs with the same identity.
  bool markSongPlayedAtStart(int songPk, qint64 unixSeconds);
  // Increments play_count for the identity of songPk.
  // This also updates all in-memory songs with the same identity.
  bool incrementPlayCount(int songPk);
  std::string songIdentityKeyBySongPk(int songPk) const;
  // Applies cloud play_count for identity key with merge=max(local, cloud).
  // Returns affected song pks for UI refresh.
  std::vector<int> applyCloudPlayCount(const std::string &identityKey,
                                       int cloudPlayCount,
                                       qint64 cloudUpdatedAt);
  std::unordered_map<std::string, int> identityPlayCounts() const;

private:
  MSong loadPreparedSongFromFile(
      const std::string &path,
      std::unordered_map<std::string, std::string> *remainingFields) const;
  void loadBuiltInSongs();
  // Loads dynamic song attributes from DB table `song_attributes` into memory.
  // Attributes whose `key` no longer has a matching definition in
  // `attribute_definitions` are treated as stale: they are skipped in memory
  // and deleted from DB instead of aborting the load.
  void loadDynamicAttributes();
  void loadComputedValues();
  void loadPlayStats();
  int songIdentityIdBySongId(int songId) const;
  int ensureSongIdentityId(const std::string &identityKey);
  void addSongToIdentityIndex(int songId, int identityId);
  void removeSongFromIdentityIndex(int songId, int identityId);
  void syncComputedValuesBySongId(int songId, const MSong &song);
  void upsertComputedFieldValueInDb(int songId,
                                    const ColumnDefinition &definition,
                                    const FieldValue &value);
  int ensureSongInDb(MSong &song);
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
  // Reverse index for fast identity-wide in-memory fan-out updates
  // (play_count / last_played_timestamp).
  std::unordered_map<int, std::vector<int>> songIdsByIdentityId_;
  mutable std::unordered_map<std::string, std::unordered_set<std::string>>
      registeredQueryFields;
  mutable std::unordered_map<std::string, std::vector<int>> registeredQueries;
};

#endif // SONGLIBRARY_H
