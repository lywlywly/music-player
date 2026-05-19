#include "songlibrary.h"
#include "databasemanager.h"
#include "songparser.h"
#include "utils.h"
#include <QDebug>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>

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
  // Order matters: built-in songs provide song_id/path/identity; subsequent
  // loaders attach additional values onto existing in-memory songs.
  loadBuiltInSongs();
  loadDynamicAttributes();
  loadComputedValues();
  loadPlayStats();
}

SongLibrary::Snapshot SongLibrary::snapshot() const {
  return Snapshot{.songs = songs,
                  .paths = paths,
                  .songIdsByIdentityId = songIdsByIdentityId_};
}

void SongLibrary::replaceWithSnapshot(Snapshot &&snapshot) {
  songs = std::move(snapshot.songs);
  paths = std::move(snapshot.paths);
  songIdsByIdentityId_ = std::move(snapshot.songIdsByIdentityId);
  registeredQueryFields.clear();
  registeredQueries.clear();
}

namespace {
class SongLibraryExprEvalContext final : public LibraryExprEvalContext {
public:
  explicit SongLibraryExprEvalContext(const MSong &song) : song_(song) {}

  const FieldValue *fieldValue(std::string_view fieldId) const override {
    auto it = song_.find(songKeyForFieldId(fieldId));
    if (it == song_.end()) {
      return nullptr;
    }
    return &it->second;
  }

private:
  static std::string songKeyForFieldId(std::string_view fieldId) {
    if (fieldId.starts_with("builtin:")) {
      return std::string(fieldId.substr(8));
    }
    return std::string(fieldId);
  }

  const MSong &song_;
};

FieldValue fieldValueFromRuntime(const ExprRuntimeValue &runtimeValue,
                                 ValueType valueType, bool &ok,
                                 const std::string &fieldId) {
  ok = true;
  switch (valueType) {
  case ValueType::Text:
    if (runtimeValue.isText()) {
      return FieldValue(runtimeValue.textValue(), fieldId);
    }
    if (runtimeValue.isBool()) {
      return FieldValue(runtimeValue.boolValueOrFalse() ? "true" : "false",
                        fieldId);
    }
    return FieldValue(
        QString::number(runtimeValue.numberValue(), 'g', 17).toStdString(),
        fieldId);
  case ValueType::Number:
    if (!runtimeValue.isNumber()) {
      ok = false;
      return FieldValue("", fieldId);
    }
    return FieldValue(
        QString::number(runtimeValue.numberValue(), 'g', 17).toStdString(),
        fieldId);
  case ValueType::Boolean:
    if (!runtimeValue.isBool()) {
      ok = false;
      return FieldValue("", fieldId);
    }
    return FieldValue(runtimeValue.boolValueOrFalse() ? "true" : "false",
                      fieldId);
  case ValueType::DateTime:
    if (!runtimeValue.isText()) {
      ok = false;
      return FieldValue("", fieldId);
    }
    if (!FieldValue::canConvert(runtimeValue.textValue(),
                                ValueType::DateTime)) {
      ok = false;
      return FieldValue("", fieldId);
    }
    return FieldValue(runtimeValue.textValue(), fieldId);
  }
  ok = false;
  return FieldValue("", fieldId);
}

std::string songIdentityKeyFromSong(const MSong &song) {
  const std::string title =
      util::normalizedText(songFieldText(song, "title")).toStdString();
  const std::string artist =
      util::normalizedText(songFieldText(song, "artist")).toStdString();
  const std::string album =
      util::normalizedText(songFieldText(song, "album")).toStdString();
  return title + "|" + artist + "|" + album;
}

void evaluateComputedFieldsInSong(MSong &song, const ColumnRegistry &registry) {
  const ExprSymbolResolver resolver(registry.expressionSymbols());
  for (auto it = song.begin(); it != song.end();) {
    if (it->first.rfind("attr:", 0) == 0) {
      ++it;
      continue;
    }
    const QString key = QString::fromStdString(it->first);
    if (registry.isBuiltInSongAttributeKey(key)) {
      ++it;
      continue;
    }
    const ColumnDefinition *definition = registry.findColumn(key);
    const FieldDefinition *field =
        definition ? registry.findField(definition->id) : nullptr;
    if (!definition || (field && field->source == ColumnSource::Computed)) {
      it = song.erase(it);
    } else {
      ++it;
    }
  }

  SongLibraryExprEvalContext context(song);
  for (const FieldDefinition &definition :
       registry.computedFieldDefinitions()) {
    ExprParseResult parsed =
        ::parseLibraryExpression(definition.expression, resolver);
    if (!parsed.ok()) {
      song.erase(definition.id.toStdString());
      continue;
    }
    const ExprRuntimeValue runtimeValue = parsed.expr->evaluateValue(context);
    bool conversionOk = false;
    const std::string fieldId = definition.id.toStdString();
    const FieldValue computedValue = fieldValueFromRuntime(
        runtimeValue, definition.valueType, conversionOk, fieldId);
    if (!conversionOk) {
      song.erase(definition.id.toStdString());
      continue;
    }
    song.insert_or_assign(fieldId, FieldValue(computedValue.text, fieldId));
  }
}
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
    syncComputedValuesBySongId(songId, s);
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
  addSongToIdentityIndex(songId, songIdentityIdBySongId(songId));
  // New song path: persist dynamic attributes after inserting into memory/DB.
  syncDynamicAttributesBySongId(songId, songs[songId]);
  syncComputedValuesBySongId(songId, songs[songId]);

  return songId;
}

ExprParseResult
SongLibrary::parseLibraryExpression(const QString &expressionText) const {
  const ExprSymbolResolver resolver(columnRegistry_.expressionSymbols());
  return ::parseLibraryExpression(expressionText, resolver);
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
    if (s.empty()) {
      continue;
    }
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
    if (s.empty()) {
      continue;
    }
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
    if (song.empty()) {
      continue;
    }
    set.insert(song.at(field).text);
  }

  return set;
}

const std::unordered_set<std::string> &
SongLibrary::registerQueryField(std::string field) const {
  std::unordered_set<std::string> set;
  for (const auto &song : songs) {
    if (song.empty()) {
      continue;
    }
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
  songIdsByIdentityId_.clear();

  const QList<QString> columns = columnRegistry_.songAttributeColumnIds();

  QStringList selectColumns;
  selectColumns.push_back("s.song_id");
  for (const QString &columnId : columns) {
    selectColumns.push_back(QStringLiteral("s.") + columnId);
  }
  selectColumns.push_back("s.identity_id");
  selectColumns.push_back("i.song_identity_key");

  const QString sql =
      QString("SELECT %1 FROM songs s "
              "JOIN song_identities i ON i.identity_id=s.identity_id "
              "ORDER BY s.song_id")
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
      m.insert_or_assign(key, FieldValue(value, key));
    }
    const int identityId = q.value(columns.size() + 1).toInt();
    m.insert_or_assign(
        "song_identity_id",
        FieldValue(std::to_string(identityId), "song_identity_id"));
    m.insert_or_assign(
        "song_identity_key",
        FieldValue(q.value(columns.size() + 2).toString().toStdString(),
                   "song_identity_key"));

    const std::string path = m.at("filepath").text;
    if (path.empty()) {
      qFatal("loadBuiltInSongs: filepath is empty for song_id=%d", id);
    }
    if (!paths.emplace(path, id).second) {
      qFatal("loadBuiltInSongs: duplicated filepath detected: %s",
             path.c_str());
    }
    addSongToIdentityIndex(id, identityId);
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
    const FieldDefinition *field = columnRegistry_.findField(columnId);
    if (!field || field->source != ColumnSource::SongAttribute) {
      qWarning() << "loadDynamicAttributes: removing stale dynamic attribute"
                 << columnId;
      unknownKeys.insert(key);
      continue;
    }
    const std::string fieldId = columnId.toStdString();
    songs[songId].insert_or_assign(fieldId, FieldValue(valueText, fieldId));
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

void SongLibrary::loadComputedValues() {
  QSqlDatabase &db = databaseManager_.db();
  QSet<QString> knownKeys;
  for (const FieldDefinition &definition :
       columnRegistry_.computedFieldDefinitions()) {
    knownKeys.insert(ColumnRegistry::computedKeyFromFieldId(definition.id));
  }

  QSqlQuery query(db);
  if (!query.exec(R"(
        SELECT DISTINCT key
        FROM song_computed_attributes
    )")) {
    qFatal("loadComputedValues query keys failed: %s",
           query.lastError().text().toUtf8().data());
  }

  QSet<QString> unknownKeys;
  while (query.next()) {
    const QString key = query.value(0).toString();
    if (!knownKeys.contains(key)) {
      unknownKeys.insert(key);
    }
  }

  if (!unknownKeys.empty()) {
    QSqlQuery remove(db);
    remove.prepare(R"(
        DELETE FROM song_computed_attributes
        WHERE key=:key
    )");
    for (const QString &key : unknownKeys) {
      remove.bindValue(":key", key);
      if (!remove.exec()) {
        qFatal("loadComputedValues delete unknown keys failed: %s",
               remove.lastError().text().toUtf8().data());
      }
    }
  }
  query = QSqlQuery(db);
  if (!query.exec(R"(
        SELECT song_id, key, value_text
        FROM song_computed_attributes
    )")) {
    qFatal("loadComputedValues values error: %s",
           query.lastError().text().toUtf8().data());
  }

  while (query.next()) {
    const int songId = query.value(0).toInt();
    const QString fieldId =
        ColumnRegistry::computedFieldId(query.value(1).toString());
    const std::string valueText = query.value(2).toString().toStdString();
    if (songId < 0) {
      qFatal("loadComputedValues: negative song_id=%d", songId);
    }
    if (songId >= static_cast<int>(songs.size())) {
      qFatal("loadComputedValues: song_id out of range: %d", songId);
    }
    const FieldDefinition *field = columnRegistry_.findField(fieldId);
    if (!field || field->source != ColumnSource::Computed ||
        field->expression.trimmed().isEmpty()) {
      continue;
    }
    const std::string fieldKey = fieldId.toStdString();
    songs[songId].insert_or_assign(fieldKey, FieldValue(valueText, fieldKey));
  }
}

void SongLibrary::loadPlayStats() {
  QSqlDatabase &db = databaseManager_.db();
  QSqlQuery query(db);
  if (!query.exec(R"(
        SELECT identity_id, play_count, last_played_timestamp
        FROM song_play_stats
    )")) {
    qFatal("loadPlayStats query failed: %s",
           query.lastError().text().toUtf8().data());
  }

  while (query.next()) {
    const int identityId = query.value(0).toInt();
    const int playCount = query.value(1).toInt();
    const qint64 lastPlayed = query.value(2).toLongLong();
    auto it = songIdsByIdentityId_.find(identityId);
    if (it == songIdsByIdentityId_.end()) {
      continue;
    }
    for (int songId : it->second) {
      songs[songId].insert_or_assign(
          "play_count", FieldValue(std::to_string(playCount), "play_count"));
      songs[songId].insert_or_assign(
          "last_played_timestamp",
          FieldValue(std::to_string(lastPlayed), "last_played_timestamp"));
    }
  }
}

int SongLibrary::songIdentityIdBySongId(int songId) const {
  if (songId < 0 || songId >= static_cast<int>(songs.size()) ||
      songs[songId].empty()) {
    return -1;
  }
  auto it = songs[songId].find("song_identity_id");
  if (it == songs[songId].end() || it->second.text.empty()) {
    return -1;
  }
  bool ok = false;
  const int identityId = QString::fromStdString(it->second.text).toInt(&ok);
  if (!ok) {
    return -1;
  }
  if (identityId <= 0) {
    return -1;
  }
  return identityId;
}

int SongLibrary::ensureSongIdentityId(const std::string &identityKey) {
  if (identityKey.empty()) {
    return -1;
  }
  QSqlDatabase &db = databaseManager_.db();
  QSqlQuery insert(db);
  insert.prepare(R"(
      INSERT OR IGNORE INTO song_identities(song_identity_key)
      VALUES(:song_identity_key)
  )");
  insert.bindValue(":song_identity_key", QString::fromStdString(identityKey));
  if (!insert.exec()) {
    qFatal("ensureSongIdentityId insert failed: %s",
           insert.lastError().text().toUtf8().data());
  }

  QSqlQuery find(db);
  find.prepare(R"(
      SELECT identity_id
      FROM song_identities
      WHERE song_identity_key=:song_identity_key
  )");
  find.bindValue(":song_identity_key", QString::fromStdString(identityKey));
  if (!find.exec() || !find.next()) {
    qFatal("ensureSongIdentityId query failed: %s",
           find.lastError().text().toUtf8().data());
  }
  return find.value(0).toInt();
}

void SongLibrary::addSongToIdentityIndex(int songId, int identityId) {
  if (identityId <= 0) {
    return;
  }
  auto &songIds = songIdsByIdentityId_[identityId];
  if (std::find(songIds.begin(), songIds.end(), songId) == songIds.end()) {
    songIds.push_back(songId);
  }
}

void SongLibrary::removeSongFromIdentityIndex(int songId, int identityId) {
  if (identityId <= 0) {
    return;
  }
  auto it = songIdsByIdentityId_.find(identityId);
  if (it == songIdsByIdentityId_.end()) {
    return;
  }
  auto &songIds = it->second;
  songIds.erase(std::remove(songIds.begin(), songIds.end(), songId),
                songIds.end());
  if (songIds.empty()) {
    songIdsByIdentityId_.erase(it);
  }
}

bool SongLibrary::markSongPlayedAtStart(int songPk, qint64 unixSeconds) {
  if (songPk < 0 || songPk >= static_cast<int>(songs.size()) ||
      songs[songPk].empty()) {
    return false;
  }

  int playCount = 0;
  auto playCountIt = songs[songPk].find("play_count");
  if (playCountIt != songs[songPk].end() &&
      playCountIt->second.valueType() == ValueType::Number) {
    playCount = static_cast<int>(playCountIt->second.typed.numberDouble);
  }

  const int identityId = songIdentityIdBySongId(songPk);
  if (identityId <= 0) {
    return false;
  }

  QSqlDatabase &db = databaseManager_.db();
  QSqlQuery query(db);
  query.prepare(R"(
      INSERT INTO song_play_stats(identity_id, play_count, last_played_timestamp)
      VALUES(:identity_id, :play_count, :last_played_timestamp)
      ON CONFLICT(identity_id) DO UPDATE SET
          last_played_timestamp=excluded.last_played_timestamp
  )");
  query.bindValue(":identity_id", identityId);
  query.bindValue(":play_count", playCount);
  query.bindValue(":last_played_timestamp", unixSeconds);
  if (!query.exec()) {
    qFatal("markSongPlayedAtStart upsert failed: %s",
           query.lastError().text().toUtf8().data());
  }
  auto identityIt = songIdsByIdentityId_.find(identityId);
  if (identityIt == songIdsByIdentityId_.end()) {
    return true;
  }
  for (int id : identityIt->second) {
    songs[id].insert_or_assign(
        "last_played_timestamp",
        FieldValue(std::to_string(unixSeconds), "last_played_timestamp"));
    if (songs[id].find("play_count") == songs[id].end()) {
      songs[id].insert_or_assign("play_count", FieldValue("0", "play_count"));
    }
  }
  return true;
}

bool SongLibrary::incrementPlayCount(int songPk) {
  if (songPk < 0 || songPk >= static_cast<int>(songs.size()) ||
      songs[songPk].empty()) {
    return false;
  }

  const int identityId = songIdentityIdBySongId(songPk);
  if (identityId <= 0) {
    return false;
  }

  QSqlDatabase &db = databaseManager_.db();
  int currentCount = 0;
  QSqlQuery readQuery(db);
  readQuery.prepare(R"(
      SELECT play_count
      FROM song_play_stats
      WHERE identity_id=:identity_id
  )");
  readQuery.bindValue(":identity_id", identityId);
  if (readQuery.exec() && readQuery.next()) {
    currentCount = readQuery.value(0).toInt();
  } else {
    auto countIt = songs[songPk].find("play_count");
    if (countIt != songs[songPk].end() &&
        countIt->second.valueType() == ValueType::Number) {
      currentCount = static_cast<int>(countIt->second.typed.numberDouble);
    }
  }
  const int newCount = currentCount + 1;

  qint64 lastPlayedTs = 0;
  auto tsIt = songs[songPk].find("last_played_timestamp");
  if (tsIt != songs[songPk].end()) {
    lastPlayedTs = QString::fromStdString(tsIt->second.text).toLongLong();
  }

  QSqlQuery query(db);
  query.prepare(R"(
      INSERT INTO song_play_stats(identity_id, play_count, last_played_timestamp)
      VALUES(:identity_id, :play_count, :last_played_timestamp)
      ON CONFLICT(identity_id) DO UPDATE SET
          play_count=excluded.play_count
  )");
  query.bindValue(":identity_id", identityId);
  query.bindValue(":play_count", newCount);
  query.bindValue(":last_played_timestamp", lastPlayedTs);
  if (!query.exec()) {
    qFatal("incrementPlayCount upsert failed: %s",
           query.lastError().text().toUtf8().data());
  }
  auto identityIt = songIdsByIdentityId_.find(identityId);
  if (identityIt == songIdsByIdentityId_.end()) {
    return true;
  }
  for (int id : identityIt->second) {
    songs[id].insert_or_assign(
        "play_count", FieldValue(std::to_string(newCount), "play_count"));
  }
  return true;
}

std::string SongLibrary::songIdentityKeyBySongPk(int songPk) const {
  if (songPk < 0 || songPk >= static_cast<int>(songs.size()) ||
      songs[songPk].empty()) {
    return {};
  }
  auto it = songs[songPk].find("song_identity_key");
  if (it == songs[songPk].end()) {
    return {};
  }
  return it->second.text;
}

std::vector<int>
SongLibrary::applyCloudPlayCount(const std::string &identityKey,
                                 int cloudPlayCount, qint64) {
  std::vector<int> affected;
  if (identityKey.empty() || cloudPlayCount < 0) {
    return affected;
  }

  const int identityId = ensureSongIdentityId(identityKey);
  if (identityId <= 0) {
    return affected;
  }

  QSqlDatabase &db = databaseManager_.db();
  qint64 lastPlayedTs = 0;
  int localCount = 0;
  QSqlQuery readQuery(db);
  readQuery.prepare(R"(
      SELECT play_count, last_played_timestamp
      FROM song_play_stats
      WHERE identity_id=:identity_id
  )");
  readQuery.bindValue(":identity_id", identityId);
  if (readQuery.exec() && readQuery.next()) {
    localCount = readQuery.value(0).toInt();
    lastPlayedTs = readQuery.value(1).toLongLong();
  }
  const int merged = std::max(localCount, cloudPlayCount);

  QSqlQuery upsert(db);
  upsert.prepare(R"(
      INSERT INTO song_play_stats(identity_id, play_count, last_played_timestamp)
      VALUES(:identity_id, :play_count, :last_played_timestamp)
      ON CONFLICT(identity_id) DO UPDATE SET
          play_count=excluded.play_count
  )");
  upsert.bindValue(":identity_id", identityId);
  upsert.bindValue(":play_count", merged);
  upsert.bindValue(":last_played_timestamp", lastPlayedTs);
  if (!upsert.exec()) {
    qFatal("applyCloudPlayCount upsert failed: %s",
           upsert.lastError().text().toUtf8().data());
  }

  auto identityIt = songIdsByIdentityId_.find(identityId);
  if (identityIt == songIdsByIdentityId_.end()) {
    return affected;
  }
  affected.reserve(identityIt->second.size());
  for (int songPk : identityIt->second) {
    songs[songPk].insert_or_assign(
        "play_count", FieldValue(std::to_string(merged), "play_count"));
    affected.push_back(songPk);
  }
  return affected;
}

std::unordered_map<std::string, int> SongLibrary::identityPlayCounts() const {
  std::unordered_map<std::string, int> counts;
  counts.reserve(songs.size());
  for (const MSong &song : songs) {
    if (song.empty()) {
      continue;
    }
    auto keyIt = song.find("song_identity_key");
    if (keyIt == song.end() || keyIt->second.text.empty()) {
      continue;
    }
    int count = 0;
    auto countIt = song.find("play_count");
    if (countIt != song.end() &&
        countIt->second.valueType() == ValueType::Number) {
      count = static_cast<int>(countIt->second.typed.numberDouble);
    }
    auto existing = counts.find(keyIt->second.text);
    if (existing == counts.end()) {
      counts.emplace(keyIt->second.text, count);
    } else {
      existing->second = std::max(existing->second, count);
    }
  }
  return counts;
}

void SongLibrary::syncComputedValuesBySongId(int songId, const MSong &song) {
  for (const FieldDefinition &definition :
       columnRegistry_.computedFieldDefinitions()) {
    auto it = song.find(definition.id.toStdString());
    if (it == song.end()) {
      continue;
    }
    songs[songId].insert_or_assign(definition.id.toStdString(), it->second);
    upsertComputedFieldValueInDb(songId, definition, it->second);
  }
}

void SongLibrary::upsertComputedFieldValueInDb(
    int songId, const FieldDefinition &definition, const FieldValue &value) {
  QSqlDatabase &db = databaseManager_.db();
  QSqlQuery query(db);
  query.prepare(R"(
      INSERT INTO song_computed_attributes(song_id, key, value_text, value_type)
      VALUES(:song_id, :key, :value_text, :value_type)
      ON CONFLICT(song_id, key) DO UPDATE SET
          value_text=excluded.value_text,
          value_type=excluded.value_type
  )");
  query.bindValue(":song_id", songId);
  query.bindValue(":key",
                  ColumnRegistry::computedKeyFromFieldId(definition.id));
  query.bindValue(":value_text", QString::fromStdString(value.text));
  query.bindValue(":value_type",
                  columnValueTypeToStorageString(definition.valueType));
  if (!query.exec()) {
    qFatal("upsert song_computed_attributes failed: %s",
           query.lastError().text().toUtf8().data());
  }
}

MSong SongLibrary::loadPreparedSongFromFile(
    const std::string &path,
    std::unordered_map<std::string, std::string> *remainingFields) const {
#ifdef MYPLAYER_TESTING
  MSong parsed = parseSong_(path, columnRegistry_, remainingFields);
#else
  MSong parsed = SongParser::parse(path, columnRegistry_, remainingFields);
#endif
  evaluateComputedFieldsInSong(parsed, columnRegistry_);
  parsed.insert_or_assign(
      "song_identity_key",
      FieldValue(songIdentityKeyFromSong(parsed), "song_identity_key"));
  return parsed;
}

const MSong &SongLibrary::refreshSongFromFile(
    const std::string &path,
    std::unordered_map<std::string, std::string> *remainingFields) {
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

  MSong parsed = loadPreparedSongFromFile(path, remainingFields);
  syncBuiltInFieldsBySongId(songId, parsed);
  syncDynamicAttributesBySongId(songId, parsed);
  syncComputedValuesBySongId(songId, parsed);
  return songs[songId];
}

MSong SongLibrary::loadSongFromFile(const std::string &path) const {
  return loadPreparedSongFromFile(path, nullptr);
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

int SongLibrary::ensureSongInDb(MSong &song) {
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
  columnNames.push_back("identity_id");
  placeholders.push_back(":identity_id");

  QSqlQuery insert(db);
  // In current add flow, insertion failure is treated as fatal.
  insert.prepare(QString("INSERT OR IGNORE INTO songs(%1) VALUES(%2)")
                     .arg(columnNames.join(", "), placeholders.join(", ")));
  for (const QString &columnId : columns) {
    const std::string key = columnId.toStdString();
    const QString value = songFieldText(song, key);
    insert.bindValue(":" + columnId, value);
  }
  const std::string identityKey = song.at("song_identity_key").text;
  const int identityId = ensureSongIdentityId(identityKey);
  if (identityId <= 0) {
    qFatal("ensureSongInDb: invalid identity id for key: %s",
           identityKey.c_str());
  }
  insert.bindValue(":identity_id", identityId);
  if (!insert.exec()) {
    qFatal("ensureSongInDb insert failed: %s",
           insert.lastError().text().toUtf8().data());
  }
  song.insert_or_assign(
      "song_identity_id",
      FieldValue(std::to_string(identityId), "song_identity_id"));

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
  assignments.push_back("identity_id=:identity_id");

  QSqlQuery updateSong(db);
  updateSong.prepare(QString("UPDATE songs SET %1 WHERE song_id=:song_id")
                         .arg(assignments.join(", ")));
  for (const QString &columnId : columns) {
    const std::string key = columnId.toStdString();
    const QString value = songFieldText(song, key);
    updateSong.bindValue(":" + columnId, value);

    songs[songId].insert_or_assign(key, FieldValue(value.toStdString(), key));
  }
  const int oldIdentityId = songIdentityIdBySongId(songId);
  const std::string identityKey = song.at("song_identity_key").text;
  const int identityId = ensureSongIdentityId(identityKey);
  updateSong.bindValue(":identity_id", identityId);
  songs[songId].insert_or_assign(
      "song_identity_id",
      FieldValue(std::to_string(identityId), "song_identity_id"));
  songs[songId].insert_or_assign("song_identity_key",
                                 FieldValue(identityKey, "song_identity_key"));
  updateSong.bindValue(":song_id", songId);
  if (!updateSong.exec()) {
    qFatal("syncBuiltInFieldsBySongId failed: %s",
           updateSong.lastError().text().toUtf8().data());
  }
  // Title/artist/album edits can move a song to another identity bucket.
  if (oldIdentityId != identityId) {
    removeSongFromIdentityIndex(songId, oldIdentityId);
    addSongToIdentityIndex(songId, identityId);
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
    const FieldDefinition *field = columnRegistry_.findField(columnId);
    if (!field) {
      qFatal("syncDynamicAttributesBySongId: unknown column id: %s",
             columnId.toStdString().c_str());
    }
    const QString valueType = columnValueTypeToStorageString(field->valueType);
    const QString valueText = QString::fromStdString(v.text);

    songs[songId].insert_or_assign(
        std::string("attr:") + attrKey,
        FieldValue(v.text, std::string("attr:") + attrKey));

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
