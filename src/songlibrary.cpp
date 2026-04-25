#include "songlibrary.h"
#include "databasemanager.h"
#include "songparser.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <thread>

#ifdef MYPLAYER_TESTING
SongLibrary::SongLibrary(const ColumnRegistry &columnRegistry,
                         DatabaseManager &databaseManager,
                         SongParseFn parseSong)
    : columnRegistry_(columnRegistry), databaseManager_(databaseManager),
      parseSong_(std::move(parseSong)) {
  if (!parseSong_) {
    parseSong_ = SongParser::parse;
  }
  songs.reserve(10);
}
#else
SongLibrary::SongLibrary(const ColumnRegistry &columnRegistry,
                         DatabaseManager &databaseManager)
    : columnRegistry_(columnRegistry), databaseManager_(databaseManager) {
  songs.reserve(10);
}
#endif

void SongLibrary::loadFromDatabase() {
  loadBuiltInSongs();
  loadDynamicAttributes();
}

namespace {
class SongLibraryExprEvalContext final : public LibraryExprEvalContext {
public:
  explicit SongLibraryExprEvalContext(const MSong &song) : song_(song) {}

  const FieldValue *fieldValue(std::string_view fieldId) const override {
    auto it = song_.find(std::string(fieldId));
    if (it == song_.end()) {
      return nullptr;
    }
    return &it->second;
  }

private:
  const MSong &song_;
};
} // namespace

int SongLibrary::addTolibrary(MSong &&s) {
  const std::string path = s.at("filepath").text;
  if (path.empty()) {
    qFatal("addTolibrary: filepath is empty");
  }

  auto pathIt = paths.find(path);
  if (pathIt != paths.end()) {
    // Existing song path: update built-in fields and dynamic attributes.
    const int songId = pathIt->second;
    syncBuiltInFieldsBySongId(songId, s);
    syncDynamicAttributesBySongId(songId, s);
    return songId;
  }

  const int songId = ensureSongInDb(s);

  for (auto &[field, set] : registeredQueryFields) {
    const std::string value = s.at(field).text;
    if (set.find(value) == set.end()) {
      set.insert(value);
    }
  }

  for (auto &[artist, vec] : registeredQueries) {
    if (s.at("artist").text == artist) {
      vec.push_back(songId);
    }
  }

  if (songId >= static_cast<int>(songs.size())) {
    songs.resize(songId + 1);
  }
  songs[songId] = std::move(s);
  paths[path] = songId;
  // New song path: persist dynamic attributes after inserting into memory/DB.
  syncDynamicAttributesBySongId(songId, songs[songId]);

  return songId;
}

ExprParseResult
SongLibrary::parseLibraryExpression(const QString &expressionText) const {
  return ::parseLibraryExpression(expressionText, columnRegistry_);
}

std::vector<int> SongLibrary::search(const Expr &expression) const {
  std::vector<int> matches;
  matches.reserve(songs.size());
  for (int songId = 0; songId < static_cast<int>(songs.size()); ++songId) {
    const MSong &song = songs[songId];
    if (song.empty()) {
      continue;
    }
    SongLibraryExprEvalContext context(song);
    if (expression.evaluate(context)) {
      matches.push_back(songId);
    }
  }
  return matches;
}

std::vector<int> SongLibrary::query(std::string artist) const {
  std::vector<int> view;
  for (int i = 0; i < songs.size(); i++) {
    const MSong &s = songs[i];
    if (s.at("artist").text == artist) {
      view.push_back(i);
    }
  }

  return view;
}

const std::vector<int> &SongLibrary::registerQuery(std::string artist) const {
  std::vector<int> view;
  for (int i = 0; i < songs.size(); i++) {
    const MSong &s = songs[i];
    if (s.at("artist").text == artist) {
      view.push_back(i);
    }
  }

  registeredQueries.emplace(artist, std::move(view));
  return registeredQueries.at(artist);
}

void SongLibrary::unregisterQuery(std::string artist) const {
  registeredQueries.erase(artist);
}

std::unordered_set<std::string>
SongLibrary::queryField(std::string field) const {
  std::unordered_set<std::string> set;
  for (const auto &song : songs) {
    set.insert(song.at(field).text);
  }

  return set;
}

const std::unordered_set<std::string> &
SongLibrary::registerQueryField(std::string field) const {
  std::unordered_set<std::string> set;
  for (const auto &song : songs) {
    set.insert(song.at(field).text);
  }

  registeredQueryFields.emplace(field, std::move(set));
  return registeredQueryFields.at(field);
}

void SongLibrary::unRegisterQueryField(std::string field) const {
  registeredQueryFields.erase(field);
}

void SongLibrary::loadBuiltInSongs() {
  QSqlDatabase &db = databaseManager_.db();
  int maxId =
      databaseManager_.scalarInt("SELECT IFNULL(MAX(song_id), 0) FROM songs");
  songs.clear();
  songs.resize(maxId + 1);
  paths.clear();
  paths.reserve(maxId + 1);

  const QList<QString> columns = columnRegistry_.songAttributeColumnIds();

  QStringList selectColumns;
  selectColumns.push_back("song_id");
  for (const QString &columnId : columns) {
    selectColumns.push_back(columnId);
  }

  const QString sql = QString("SELECT %1 FROM songs ORDER BY song_id")
                          .arg(selectColumns.join(", "));

  QSqlQuery q(db);
  if (!q.exec(sql)) {
    qFatal("SongLibrary loadBuiltInSongs error: %s",
           q.lastError().text().toUtf8().data());
  }

  while (q.next()) {
    int id = q.value(0).toInt();
    if (id < 0) {
      qFatal("loadBuiltInSongs: negative song_id=%d", id);
    }
    if (id >= (int)songs.size())
      songs.resize(id + 1);

    auto &m = songs[id];
    m.reserve(columns.size());
    for (int col = 0; col < columns.size(); ++col) {
      const QString &columnId = columns[col];
      const std::string key = columnId.toStdString();
      const std::string value = q.value(col + 1).toString().toStdString();
      const ColumnDefinition *definition = columnRegistry_.findColumn(columnId);
      m[key] = FieldValue::fromDefinition(definition, value);
    }

    const std::string path = m["filepath"].text;
    if (path.empty()) {
      qFatal("loadBuiltInSongs: filepath is empty for song_id=%d", id);
    }
    if (!paths.emplace(path, id).second) {
      qFatal("loadBuiltInSongs: duplicated filepath detected: %s",
             path.c_str());
    }
  }
}

const MSong &SongLibrary::getSongByPK(int pk) const { return songs[pk]; }

void SongLibrary::loadDynamicAttributes() {
  QSqlDatabase &db = databaseManager_.db();
  for (MSong &song : songs) {
    for (auto it = song.begin(); it != song.end();) {
      if (it->first.rfind("attr:", 0) == 0) {
        it = song.erase(it);
      } else {
        ++it;
      }
    }
  }

  QSqlQuery attrQuery(db);
  if (!attrQuery.exec(R"(
        SELECT song_id, key, value_text, value_type
        FROM song_attributes
    )")) {
    qFatal("loadDynamicAttributes values error: %s",
           attrQuery.lastError().text().toUtf8().data());
  }

  std::unordered_set<QString> unknownKeys;
  while (attrQuery.next()) {
    const int songId = attrQuery.value(0).toInt();
    const QString key = attrQuery.value(1).toString();
    const std::string valueText = attrQuery.value(2).toString().toStdString();
    if (songId < 0) {
      qFatal("loadDynamicAttributes: negative song_id=%d", songId);
    }
    if (songId >= static_cast<int>(songs.size())) {
      qFatal("loadDynamicAttributes: song_id out of range: %d", songId);
    }
    const QString columnId = QStringLiteral("attr:") + key;
    const ColumnDefinition *definition = columnRegistry_.findColumn(columnId);
    if (!definition) {
      qWarning() << "loadDynamicAttributes: removing stale dynamic attribute"
                 << columnId;
      unknownKeys.insert(key);
      continue;
    }
    songs[songId][columnId.toStdString()] =
        FieldValue(valueText, definition->valueType);
  }

  if (!unknownKeys.empty()) {
    QSqlQuery deleteUnknown(db);
    deleteUnknown.prepare(R"(
        DELETE FROM song_attributes
        WHERE key=:key
    )");
    for (const QString &key : unknownKeys) {
      deleteUnknown.bindValue(":key", key);
      if (!deleteUnknown.exec()) {
        qFatal("loadDynamicAttributes delete stale attributes failed: %s",
               deleteUnknown.lastError().text().toUtf8().data());
      }
    }
  }
}

bool SongLibrary::loadPlaylistState(int playlistId, int &lastPlayed,
                                    std::vector<int> &songIds) {
  if (playlistId <= 0) {
    qWarning() << "loadPlaylistState: invalid playlist id:" << playlistId;
    return false;
  }

  QSqlDatabase &db = databaseManager_.db();
  const QString playlistName = (playlistId == 1)
                                   ? QStringLiteral("Default Playlist")
                                   : QString("Playlist %1").arg(playlistId);

  QSqlQuery ensurePlaylist(db);
  ensurePlaylist.prepare(
      "INSERT OR IGNORE INTO playlists(playlist_id, name, last_played) "
      "VALUES(:playlist_id, :name, :last_played)");
  ensurePlaylist.bindValue(":playlist_id", playlistId);
  ensurePlaylist.bindValue(":name", playlistName);
  ensurePlaylist.bindValue(":last_played", 1);
  if (!ensurePlaylist.exec()) {
    qWarning() << "loadPlaylistState ensure playlist failed:"
               << ensurePlaylist.lastError().text();
    return false;
  }

  songIds.clear();
  QSqlQuery qItems(db);
  qItems.prepare(R"(
      SELECT song_id
      FROM playlist_items
      WHERE playlist_id=:playlist_id
      ORDER BY position ASC
  )");
  qItems.bindValue(":playlist_id", playlistId);
  if (!qItems.exec()) {
    qWarning() << "loadPlaylistState load items failed:"
               << qItems.lastError().text();
    return false;
  }
  while (qItems.next()) {
    songIds.push_back(qItems.value(0).toInt());
  }

  QSqlQuery qPlaylist(db);
  qPlaylist.prepare(R"(
      SELECT last_played
      FROM playlists
      WHERE playlist_id=:playlist_id
  )");
  qPlaylist.bindValue(":playlist_id", playlistId);
  if (!qPlaylist.exec()) {
    qWarning() << "loadPlaylistState load metadata failed:"
               << qPlaylist.lastError().text();
    return false;
  }
  lastPlayed = 1;
  if (qPlaylist.next()) {
    lastPlayed = qPlaylist.value(0).toInt();
  }
  return true;
}

const MSong &SongLibrary::refreshSongFromFile(const std::string &path) {
  if (path.empty()) {
    qFatal("refreshSongFromFile: filepath is empty");
  }
  auto pathIt = paths.find(path);
  if (pathIt == paths.end()) {
    qFatal("refreshSongFromFile: filepath not found in memory: %s",
           path.c_str());
  }

  const int songId = pathIt->second;
  if (songId < 0 || songId >= static_cast<int>(songs.size())) {
    qFatal("refreshSongFromFile: invalid song id=%d for filepath=%s", songId,
           path.c_str());
  }

#ifdef MYPLAYER_TESTING
  const MSong parsed = parseSong_(path, columnRegistry_);
#else
  const MSong parsed = SongParser::parse(path, columnRegistry_);
#endif
  syncBuiltInFieldsBySongId(songId, parsed);
  syncDynamicAttributesBySongId(songId, parsed);
  return songs[songId];
}

void SongLibrary::refreshSongsFromFilepaths(
    const std::vector<std::string> &filepaths,
    const std::function<void(int current, int total)> &progressCallback) {
  std::unordered_set<std::string> seen;
  std::vector<std::string> uniqueFilepaths;
  uniqueFilepaths.reserve(filepaths.size());
  for (const std::string &path : filepaths) {
    if (path.empty()) {
      qFatal("refreshSongsFromFilepaths: filepath is empty");
    }
    if (!seen.insert(path).second) {
      continue;
    }
    uniqueFilepaths.push_back(path);
  }

  const int total = static_cast<int>(uniqueFilepaths.size());
  for (int i = 0; i < total; ++i) {
    refreshSongFromFile(uniqueFilepaths[i]);
    if (progressCallback) {
      progressCallback(i + 1, total);
    }
  }
}

void SongLibrary::appendSongToPlaylistInDb(int playlistId, int songId,
                                           int position) {
  QSqlDatabase &db = databaseManager_.db();
  QSqlQuery q(db);
  q.prepare(R"(
      INSERT INTO playlist_items(playlist_id, song_id, position)
      VALUES(:playlist_id, :song_id, :position)
  )");
  q.bindValue(":playlist_id", playlistId);
  q.bindValue(":song_id", songId);
  q.bindValue(":position", position);
  if (!q.exec()) {
    qFatal("appendSongToPlaylistInDb failed: %s",
           q.lastError().text().toUtf8().data());
  }
}

void SongLibrary::removePlaylistItemsInDb(int playlistId) {
  if (playlistId <= 0) {
    return;
  }

  QSqlDatabase &mainDb = databaseManager_.db();
  const QString dbPath = mainDb.databaseName();
  const QString deleteSql =
      QString("DELETE FROM playlist_items WHERE playlist_id=%1")
          .arg(playlistId);
  if (dbPath.isEmpty() || dbPath == ":memory:") {
    QSqlQuery q(mainDb);
    if (!q.exec(deleteSql)) {
      qFatal("removePlaylistItemsInDb failed: %s",
             q.lastError().text().toUtf8().data());
    }
    return;
  }

  const QString connectionName =
      QStringLiteral("remove_playlist_items_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  std::thread([dbPath, deleteSql, connectionName]() {
    QSqlDatabase workerDb =
        QSqlDatabase::addDatabase("QSQLITE", connectionName);
    workerDb.setDatabaseName(dbPath);
    if (!workerDb.open()) {
      qWarning() << "removePlaylistItemsInDb worker open failed:"
                 << workerDb.lastError().text();
      workerDb = QSqlDatabase();
      QSqlDatabase::removeDatabase(connectionName);
      return;
    }

    {
      QSqlQuery q(workerDb);
      if (!q.exec(deleteSql)) {
        qWarning() << "removePlaylistItemsInDb worker delete failed:"
                   << q.lastError().text();
      }
    }

    workerDb.close();
    workerDb = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  }).detach();
}

int SongLibrary::ensureSongInDb(const MSong &song) {
  std::string path = song.at("filepath").text;
  if (path.empty()) {
    qFatal("ensureSongInDb: filepath is empty");
  }

  QSqlDatabase &db = databaseManager_.db();
  const QList<QString> columns = columnRegistry_.songAttributeColumnIds();

  QStringList columnNames;
  QStringList placeholders;
  for (const QString &columnId : columns) {
    columnNames.push_back(columnId);
    placeholders.push_back(":" + columnId);
  }

  QSqlQuery insert(db);
  // In current add flow, insertion failure is treated as fatal.
  insert.prepare(QString("INSERT OR IGNORE INTO songs(%1) VALUES(%2)")
                     .arg(columnNames.join(", "), placeholders.join(", ")));
  for (const QString &columnId : columns) {
    const std::string key = columnId.toStdString();
    const QString value = songFieldText(song, key);
    insert.bindValue(":" + columnId, value);
  }
  if (!insert.exec()) {
    qFatal("ensureSongInDb insert failed: %s",
           insert.lastError().text().toUtf8().data());
  }

  QSqlQuery find(db);
  find.prepare("SELECT song_id FROM songs WHERE filepath=:path");
  find.bindValue(":path", QString::fromStdString(path));
  if (!find.exec() || !find.next()) {
    qFatal("ensureSongInDb query failed: %s",
           find.lastError().text().toUtf8().data());
  }
  return find.value(0).toInt();
}

void SongLibrary::syncBuiltInFieldsBySongId(int songId, const MSong &song) {
  if (songId < 0 || songId >= static_cast<int>(songs.size())) {
    qFatal("syncBuiltInFieldsBySongId: invalid song_id=%d", songId);
  }
  QSqlDatabase &db = databaseManager_.db();
  const QList<QString> columns = columnRegistry_.songAttributeColumnIds();

  QStringList assignments;
  for (const QString &columnId : columns) {
    assignments.push_back(QString("%1=:%1").arg(columnId));
  }

  QSqlQuery updateSong(db);
  updateSong.prepare(QString("UPDATE songs SET %1 WHERE song_id=:song_id")
                         .arg(assignments.join(", ")));
  for (const QString &columnId : columns) {
    const std::string key = columnId.toStdString();
    const QString value = songFieldText(song, key);
    updateSong.bindValue(":" + columnId, value);

    const ColumnDefinition *definition = columnRegistry_.findColumn(columnId);
    songs[songId][key] =
        FieldValue::fromDefinition(definition, value.toStdString());
  }
  updateSong.bindValue(":song_id", songId);
  if (!updateSong.exec()) {
    qFatal("syncBuiltInFieldsBySongId failed: %s",
           updateSong.lastError().text().toUtf8().data());
  }
}

void SongLibrary::syncDynamicAttributesBySongId(int songId, const MSong &song) {
  QSqlDatabase &db = databaseManager_.db();
  if (songId < 0 || songId >= static_cast<int>(songs.size())) {
    qFatal("syncDynamicAttributesBySongId: invalid song_id=%d", songId);
  }
  QSqlQuery upsertAttr(db);
  upsertAttr.prepare(R"(
      INSERT INTO song_attributes(song_id, key, value_text, value_type)
      VALUES(:song_id, :key, :value_text, :value_type)
      ON CONFLICT(song_id, key) DO UPDATE SET
          value_text=excluded.value_text,
          value_type=excluded.value_type
  )");

  for (const auto &[k, v] : song) {
    if (!k.starts_with("attr:")) {
      continue;
    }
    const std::string attrKey = k.substr(5);
    if (attrKey.empty()) {
      qFatal("syncDynamicAttributesBySongId: empty attr key");
    }

    const QString columnId =
        QString::fromStdString(std::string{"attr:"} + attrKey);
    const ColumnDefinition *definition = columnRegistry_.findColumn(columnId);
    if (!definition) {
      qFatal("syncDynamicAttributesBySongId: unknown column id: %s",
             columnId.toStdString().c_str());
    }
    const QString valueType =
        columnValueTypeToStorageString(definition->valueType);
    const QString valueText = QString::fromStdString(v.text);

    songs[songId][std::string("attr:") + attrKey] =
        FieldValue(v.text, definition->valueType);

    upsertAttr.bindValue(":song_id", songId);
    upsertAttr.bindValue(":key", QString::fromStdString(attrKey));
    upsertAttr.bindValue(":value_text", valueText);
    upsertAttr.bindValue(":value_type", valueType);
    if (!upsertAttr.exec()) {
      qFatal("upsert song_attributes failed: %s",
             upsertAttr.lastError().text().toUtf8().data());
    }
  }
}
