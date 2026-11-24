#ifndef SONGLIBRARY_H
#define SONGLIBRARY_H

#include "columnregistry.h"
#include "fieldvalue.h"
#include "libraryexpression_parser.h"
#include "providedfields.h"
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using MSong = std::unordered_map<std::string, FieldValue>;

// The song's value for an expression field id (`builtin:<name>` is stored
// under `<name>`), or nullptr when the song has none.
const FieldValue *findSongFieldValue(const MSong &song,
                                     std::string_view fieldId);

inline QString songFieldText(const MSong &song, const std::string &field) {
  auto it = song.find(field);
  if (it == song.end()) {
    return QStringLiteral("");
  }
  return QString::fromStdString(it->second.text);
}

class DatabaseManager;
#ifdef MYPLAYER_TESTING
using SongParseFn = std::function<std::optional<MSong>(
    const std::string &, const ColumnRegistry &,
    std::unordered_map<std::string, std::string> *)>;
#endif

// SongLibrary is the canonical repository for song metadata in memory and in
// the database. Prefer song_id (pk) for cross-component references; filepath
// is mainly used for dedup and parser refresh lookup. Songs are indexed by
// song_id and filepath in memory. It also keeps a copy of the fields other
// components provide (plugins' ISongFieldProvider): each source groups songs
// by a key computed from their tag fields, and songs with the same key share
// its values (docs/plugins/playstats.md, "What SongLibrary keeps").
// Playlist metadata and playlist_items persistence are handled outside
// SongLibrary.
// Normal single-song updates are synchronous; bulk loads and refreshes may take
// time.
class SongLibrary {
public:
  // A provided source's grouping of the songs, memory only.
  struct ProvidedKeys {
    // By song pk; empty for no song.
    std::vector<QString> keyBySongPk;
    // No empty lists.
    std::unordered_map<QString, std::vector<int>> songsByKey;
  };
  struct Snapshot {
    std::vector<MSong> songs;
    std::unordered_map<std::string, int> paths;
    // By source id.
    std::map<QString, ProvidedKeys> providedKeys;
  };

#ifdef MYPLAYER_TESTING
  explicit SongLibrary(const ColumnRegistry &columnRegistry,
                       DatabaseManager &databaseManager,
                       SongParseFn parseSong = {});
#else
  explicit SongLibrary(const ColumnRegistry &columnRegistry,
                       DatabaseManager &databaseManager);
#endif
  // Adds a fully prepared song map to library/DB and returns primary key.
  // First rewrites filepath to its canonical form
  // (QFileInfo::canonicalFilePath, or the cleaned absolute path when the file
  // does not exist), so one file never gets two rows. If that path already
  // exists, syncs DB and in-memory fields from prepared song instead of
  // creating a new row.
  int addTolibrary(MSong &&);
  // Fills the computed fields of a song parsed by SongParser::parse, using
  // the live ColumnRegistry. loadSongFromFile does this itself; callers that
  // parse elsewhere (the library scanner) call it before addTolibrary.
  void prepareParsedSong(MSong &song) const;
  const MSong &getSongByPK(int) const;
  ExprParseResult parseLibraryExpression(const QString &expressionText) const;
  // The songs of candidatePks that evaluate the expression to true, in
  // filepath order. Removed pks are skipped.
  std::vector<int> search(const Expr &expression,
                          const std::vector<int> &candidatePks) const;
  // Whether the song evaluates the expression to true; false for a removed
  // pk.
  bool matches(const Expr &expression, int songPk) const;
  std::vector<int> query(std::string) const;
  const std::vector<int> &registerQuery(std::string) const;
  void unregisterQuery(std::string) const;
  std::unordered_set<std::string> queryField(std::string) const;
  const std::unordered_set<std::string> &registerQueryField(std::string) const;
  void unRegisterQueryField(std::string) const;
  // Deletes the songs from DB table `songs` (their attributes and
  // playlist_items rows cascade) and from memory, including registered
  // queries and the provided sources' keys. A re-added song gets a new pk, as
  // pks are never reused. getSongByPK returns an empty song for a removed pk.
  // songPks must be distinct songs in the library.
  void removeSongs(const std::vector<int> &songPks);
  // TODO: serialize
  // TODO: deserialize
  // Loads the full song set (built-in columns + dynamic attributes) from DB
  // into memory, and each provided source's keys. This can take noticeable
  // time on large libraries. The provided values are not read: see
  // fillProvidedValues.
  void loadFromDatabase();
  // After loadFromDatabase: gives the songs of each key in values its
  // values, for the source sourceId (an allValues result).
  void fillProvidedValues(const QString &sourceId,
                          const ProvidedValuesByKey &values);
  // Moves the songs and keys out, leaving the library empty: the worker's
  // library is discarded right after (docs/plugins/playstats.md, "Startup").
  Snapshot takeSnapshot();
  // A load runs, whose snapshot replaces the songs: until
  // replaceWithSnapshot, refreshStale reads nothing and keeps the stale keys,
  // so a change marked meanwhile is read for the loaded songs.
  void beginLoad();
  // Takes the snapshot's songs and keys, keeps the stale keys, ends the load
  // and refreshes them (the songs changed need no notification: nothing shows
  // them before the load ends).
  void replaceWithSnapshot(Snapshot &&snapshot);
  // Re-parses one song by filepath, updates DB + in-memory fields, and returns
  // the refreshed in-memory song. When remainingFields is provided, parser
  // leftovers (unbound tags) are written there for caller-side use. If the file
  // cannot be parsed, stored fields are left unchanged and returned as-is.
  const MSong &refreshSongFromFile(
      const std::string &path,
      std::unordered_map<std::string, std::string> *remainingFields = nullptr);
  // Parses one file and returns a prepared song map containing:
  // built-in fields, dynamic attrs from parser and computed fields. Returns
  // std::nullopt when the file cannot be parsed.
  std::optional<MSong> loadSongFromFile(const std::string &path) const;
  // Re-parses a filepath list (deduped internally), then updates DB and
  // in-memory fields for each song. This can take noticeable time on large
  // lists; progressCallback receives (current, total) after each refresh for
  // progress UI.
  void refreshSongsFromFilepaths(
      const std::vector<std::string> &filepaths,
      const std::function<void(int current, int total)> &progressCallback = {});

  // Provided fields (docs/plugins/playstats.md, "What SongLibrary keeps").
  // Adds a source, before the library loads; its field ids are registered
  // in the ColumnRegistry.
  void addProvidedSource(ProvidedFields provider);
  // The sources, for the load's worker library.
  std::vector<ProvidedFields> providedSources() const;
  // Called when the library goes from no stale keys to some, so the caller
  // queues one refreshStale.
  void setStaleCallback(std::function<void()> onStale);
  // The values of these keys of the source changed; they are read at the
  // next refreshStale. Keys of an unknown source are ignored (none was
  // registered for it).
  void markStale(const QString &sourceId, const QStringList &keys);
  // Reads the stale keys that songs have, one values() call per source, and
  // replaces those songs' provided fields; returns their pks. During a load,
  // reads nothing and returns none (see beginLoad).
  std::vector<int> refreshStale();

private:
  struct ProvidedSource {
    ProvidedFields provider;
    ProvidedKeys keys;
    // Values to read at the next refresh.
    QSet<QString> staleKeys;
  };

  // The key of a song for a source, from its key fields' text.
  static QString providedKey(const ProvidedFields &provider, const MSong &song);
  // Files the song under its key in source.
  void indexProvidedKey(ProvidedSource &source, int songId);
  void unindexProvidedKey(ProvidedSource &source, int songId);
  // A key's values (one per field; null: none) as the song's field values,
  // converted by the fields' types; no value where there is none.
  static std::vector<std::optional<FieldValue>>
  toFieldValues(const ProvidedFields &provider, const QList<QVariant> *values);
  // Sets the song's provided fields, removing those with no value.
  static void
  setProvidedFields(const ProvidedFields &provider, MSong &song,
                    const std::vector<std::optional<FieldValue>> &fieldValues);
  void markStale(ProvidedSource &source, const QString &key);
  std::optional<MSong> loadPreparedSongFromFile(
      const std::string &path,
      std::unordered_map<std::string, std::string> *remainingFields) const;
  void loadBuiltInSongs();
  // Loads dynamic song attributes from DB table `song_attributes` into memory.
  // Attributes whose `key` no longer has a matching definition in
  // `attribute_definitions` are treated as stale: they are skipped in memory
  // and deleted from DB instead of aborting the load.
  void loadDynamicAttributes();
  void loadComputedValues();
  void loadProvidedKeys();
  void syncComputedValuesBySongId(int songId, const MSong &song);
  void upsertComputedFieldValueInDb(int songId,
                                    const FieldDefinition &definition,
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
  // By source id.
  std::map<QString, ProvidedSource> providedSources_;
  std::function<void()> onStale_;
  bool hasStaleKeys_ = false;
  bool loading_ = false;
  mutable std::unordered_map<std::string, std::unordered_set<std::string>>
      registeredQueryFields;
  mutable std::unordered_map<std::string, std::vector<int>> registeredQueries;
};

// SongLibrary::parseLibraryExpression, then rejects an expression whose
// static type is not boolean with "Expression must be a condition". For
// expressions that filter songs (autoplaylists).
ExprParseResult parseSongFilter(const SongLibrary &library,
                                const QString &expressionText);

#endif // SONGLIBRARY_H
