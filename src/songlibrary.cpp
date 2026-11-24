#include "songlibrary.h"
#include "databasemanager.h"
#include "songparser.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>
#include <utility>

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
  // Order matters: built-in songs provide song_id/path and the key fields;
  // subsequent loaders attach additional values onto existing in-memory
  // songs.
  loadBuiltInSongs();
  loadDynamicAttributes();
  loadComputedValues();
  loadProvidedKeys();
}

SongLibrary::Snapshot SongLibrary::takeSnapshot() {
  Snapshot snapshot{.songs = std::move(songs), .paths = std::move(paths)};
  for (auto &[sourceId, source] : providedSources_) {
    snapshot.providedKeys.emplace(sourceId, std::move(source.keys));
  }
  return snapshot;
}

void SongLibrary::beginLoad() { loading_ = true; }

void SongLibrary::replaceWithSnapshot(Snapshot &&snapshot) {
  songs = std::move(snapshot.songs);
  paths = std::move(snapshot.paths);
  for (auto &[sourceId, keys] : snapshot.providedKeys) {
    providedSources_.at(sourceId).keys = std::move(keys);
  }
  registeredQueryFields.clear();
  registeredQueries.clear();
  loading_ = false;
  // The keys marked stale during the load, read for the loaded songs.
  refreshStale();
}

const FieldValue *findSongFieldValue(const MSong &song,
                                     std::string_view fieldId) {
  constexpr std::string_view kBuiltinPrefix = "builtin:";
  if (fieldId.starts_with(kBuiltinPrefix)) {
    fieldId.remove_prefix(kBuiltinPrefix.size());
  }
  const auto it = song.find(std::string(fieldId));
  return it == song.end() ? nullptr : &it->second;
}

namespace {
class SongLibraryExprEvalContext final : public LibraryExprEvalContext {
public:
  explicit SongLibraryExprEvalContext(const MSong &song) : song_(song) {}

  const FieldValue *fieldValue(std::string_view fieldId) const override {
    return findSongFieldValue(song_, fieldId);
  }

private:
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

namespace {
std::string canonicalSongPath(const std::string &path) {
  const QFileInfo info(QString::fromStdString(path));
  QString canonical = info.canonicalFilePath();
  if (canonical.isEmpty()) {
    canonical = QDir::cleanPath(info.absoluteFilePath());
  }
  return canonical.toStdString();
}
} // namespace

int SongLibrary::addTolibrary(MSong &&s) {
  if (s.at("filepath").text.empty()) {
    qFatal("addTolibrary: filepath is empty");
  }
  const std::string path = canonicalSongPath(s.at("filepath").text);
  s.insert_or_assign("filepath", FieldValue(path, "filepath"));

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
  // Its provided fields are read at the next refresh, batched with the
  // other songs a scan or a folder adds (docs/plugins/playstats.md, "One
  // path for every change after the load").
  for (auto &[sourceId, source] : providedSources_) {
    indexProvidedKey(source, songId);
    markStale(source, source.keys.keyBySongPk[songId]);
  }
  // New song path: persist dynamic attributes after inserting into memory/DB.
  syncDynamicAttributesBySongId(songId, songs[songId]);
  syncComputedValuesBySongId(songId, songs[songId]);

  return songId;
}

void SongLibrary::removeSongs(const std::vector<int> &songPks) {
  QSqlDatabase &db = databaseManager_.db();
  if (!db.transaction()) {
    qFatal("removeSongs: failed to start transaction: %s",
           db.lastError().text().toUtf8().data());
  }
  QSqlQuery q(db);
  q.prepare("DELETE FROM songs WHERE song_id=:song_id");
  for (int songPk : songPks) {
    q.bindValue(":song_id", songPk);
    if (!q.exec()) {
      qFatal("removeSongs delete failed: %s",
             q.lastError().text().toUtf8().data());
    }
  }
  if (!db.commit()) {
    qFatal("removeSongs commit failed: %s",
           db.lastError().text().toUtf8().data());
  }

  for (int songPk : songPks) {
    for (auto &[sourceId, source] : providedSources_) {
      unindexProvidedKey(source, songPk);
    }
    const MSong removed = std::exchange(songs.at(songPk), MSong{});
    paths.erase(removed.at("filepath").text);
    for (auto &[artist, songIds] : registeredQueries) {
      std::erase(songIds, songPk);
    }
    for (auto &[field, values] : registeredQueryFields) {
      const std::string &value = removed.at(field).text;
      const bool valueStillUsed =
          std::any_of(songs.begin(), songs.end(), [&](const MSong &song) {
            return !song.empty() && song.at(field).text == value;
          });
      if (!valueStillUsed) {
        values.erase(value);
      }
    }
  }
}

ExprParseResult
SongLibrary::parseLibraryExpression(const QString &expressionText) const {
  const ExprSymbolResolver resolver(columnRegistry_.expressionSymbols());
  return ::parseLibraryExpression(expressionText, resolver);
}

std::vector<int>
SongLibrary::search(const Expr &expression,
                    const std::vector<int> &candidatePks) const {
  std::vector<int> found;
  for (int songPk : candidatePks) {
    if (matches(expression, songPk)) {
      found.push_back(songPk);
    }
  }
  std::sort(found.begin(), found.end(), [this](int a, int b) {
    return songs[a].at("filepath").text < songs[b].at("filepath").text;
  });
  return found;
}

bool SongLibrary::matches(const Expr &expression, int songPk) const {
  if (songPk < 0 || songPk >= static_cast<int>(songs.size()) ||
      songs[songPk].empty()) {
    return false;
  }
  SongLibraryExprEvalContext context(songs[songPk]);
  return expression.evaluate(context);
}

ExprParseResult parseSongFilter(const SongLibrary &library,
                                const QString &expressionText) {
  ExprParseResult result = library.parseLibraryExpression(expressionText);
  if (result.ok() &&
      inferExprStaticType(*result.expr) != ExprStaticType::Bool) {
    result.expr.reset();
    result.error = {.message =
                        QStringLiteral("Expression must be a condition")};
  }
  return result;
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

  const QList<QString> columns = columnRegistry_.songAttributeColumnIds();

  QStringList selectColumns;
  selectColumns.push_back("s.song_id");
  for (const QString &columnId : columns) {
    selectColumns.push_back(QStringLiteral("s.") + columnId);
  }

  const QString sql = QString("SELECT %1 FROM songs s ORDER BY s.song_id")
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

    const std::string path = m.at("filepath").text;
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

void SongLibrary::addProvidedSource(ProvidedFields provider) {
  const QString sourceId = provider.sourceId;
  ProvidedSource &source =
      providedSources_
          .emplace(sourceId, ProvidedSource{.provider = std::move(provider)})
          .first->second;
  for (int songId = 0; songId < static_cast<int>(songs.size()); ++songId) {
    indexProvidedKey(source, songId);
  }
}

std::vector<ProvidedFields> SongLibrary::providedSources() const {
  std::vector<ProvidedFields> sources;
  for (const auto &[sourceId, source] : providedSources_) {
    sources.push_back(source.provider);
  }
  return sources;
}

void SongLibrary::setStaleCallback(std::function<void()> onStale) {
  onStale_ = std::move(onStale);
}

void SongLibrary::loadProvidedKeys() {
  for (auto &[sourceId, source] : providedSources_) {
    source.keys = {};
    for (int songId = 0; songId < static_cast<int>(songs.size()); ++songId) {
      indexProvidedKey(source, songId);
    }
  }
}

void SongLibrary::fillProvidedValues(const QString &sourceId,
                                     const ProvidedValuesByKey &values) {
  ProvidedSource &source = providedSources_.at(sourceId);
  // Walks the rows, most keys having none, rather than every song's key; each
  // row's values are converted once and copied into the key's songs.
  for (auto it = values.cbegin(); it != values.cend(); ++it) {
    const auto songsIt = source.keys.songsByKey.find(it.key());
    if (songsIt == source.keys.songsByKey.end()) {
      continue;
    }
    const std::vector<std::optional<FieldValue>> fieldValues =
        toFieldValues(source.provider, &it.value());
    for (int songId : songsIt->second) {
      setProvidedFields(source.provider, songs[songId], fieldValues);
    }
  }
}

void SongLibrary::markStale(const QString &sourceId, const QStringList &keys) {
  const auto it = providedSources_.find(sourceId);
  if (it == providedSources_.end()) {
    return;
  }
  for (const QString &key : keys) {
    markStale(it->second, key);
  }
}

void SongLibrary::markStale(ProvidedSource &source, const QString &key) {
  if (key.isEmpty()) {
    return;
  }
  source.staleKeys.insert(key);
  // Once per batch: the caller's refresh reads every key marked until it
  // runs.
  if (!hasStaleKeys_) {
    hasStaleKeys_ = true;
    if (onStale_) {
      onStale_();
    }
  }
}

std::vector<int> SongLibrary::refreshStale() {
  if (loading_) {
    return {};
  }
  hasStaleKeys_ = false;
  std::vector<int> changed;
  for (auto &[sourceId, source] : providedSources_) {
    // Resolved to songs now, so a song removed since it was marked is
    // skipped, and keys no song has are not read.
    QStringList keys;
    for (const QString &key : std::as_const(source.staleKeys)) {
      if (source.keys.songsByKey.contains(key)) {
        keys.push_back(key);
      }
    }
    source.staleKeys.clear();
    if (keys.isEmpty()) {
      continue;
    }
    const ProvidedValuesByKey values = source.provider.values(keys);
    for (const QString &key : std::as_const(keys)) {
      const auto valuesIt = values.find(key);
      const std::vector<std::optional<FieldValue>> fieldValues =
          toFieldValues(source.provider,
                        valuesIt == values.end() ? nullptr : &valuesIt.value());
      for (int songId : source.keys.songsByKey.at(key)) {
        setProvidedFields(source.provider, songs[songId], fieldValues);
        changed.push_back(songId);
      }
    }
  }
  std::sort(changed.begin(), changed.end());
  changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
  return changed;
}

QString SongLibrary::providedKey(const ProvidedFields &provider,
                                 const MSong &song) {
  QStringList keyFieldValues;
  keyFieldValues.reserve(static_cast<qsizetype>(provider.keyFields.size()));
  for (const std::string &keyField : provider.keyFields) {
    keyFieldValues.push_back(songFieldText(song, keyField));
  }
  return provider.songKey(keyFieldValues);
}

void SongLibrary::indexProvidedKey(ProvidedSource &source, int songId) {
  std::vector<QString> &keyBySongPk = source.keys.keyBySongPk;
  if (songId >= static_cast<int>(keyBySongPk.size())) {
    keyBySongPk.resize(songs.size());
  }
  if (songs[songId].empty()) {
    return;
  }
  QString key = providedKey(source.provider, songs[songId]);
  if (key.isEmpty()) {
    return;
  }
  source.keys.songsByKey[key].push_back(songId);
  keyBySongPk[songId] = std::move(key);
}

void SongLibrary::unindexProvidedKey(ProvidedSource &source, int songId) {
  QString &key = source.keys.keyBySongPk[songId];
  if (key.isEmpty()) {
    return;
  }
  const auto it = source.keys.songsByKey.find(key);
  std::erase(it->second, songId);
  if (it->second.empty()) {
    source.keys.songsByKey.erase(it);
  }
  key.clear();
}

std::vector<std::optional<FieldValue>>
SongLibrary::toFieldValues(const ProvidedFields &provider,
                           const QList<QVariant> *values) {
  std::vector<std::optional<FieldValue>> fieldValues(provider.fields.size());
  if (values == nullptr) {
    return fieldValues;
  }
  for (size_t i = 0; i < provider.fields.size(); ++i) {
    const QVariant value = values->value(static_cast<qsizetype>(i));
    if (!value.isValid()) {
      continue;
    }
    // By the source's type: the load's worker registry has no plugin fields
    // (docs/plugins/playstats.md, "What SongLibrary keeps").
    const ProvidedFields::Field &field = provider.fields[i];
    fieldValues[i].emplace(std::to_string(value.toLongLong()), field.id,
                           field.type);
  }
  return fieldValues;
}

void SongLibrary::setProvidedFields(
    const ProvidedFields &provider, MSong &song,
    const std::vector<std::optional<FieldValue>> &fieldValues) {
  for (size_t i = 0; i < provider.fields.size(); ++i) {
    if (fieldValues[i]) {
      song.insert_or_assign(provider.fields[i].id, *fieldValues[i]);
    } else {
      song.erase(provider.fields[i].id);
    }
  }
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

std::optional<MSong> SongLibrary::loadPreparedSongFromFile(
    const std::string &path,
    std::unordered_map<std::string, std::string> *remainingFields) const {
#ifdef MYPLAYER_TESTING
  std::optional<MSong> parsed =
      parseSong_(path, columnRegistry_, remainingFields);
#else
  std::optional<MSong> parsed =
      SongParser::parse(path, columnRegistry_, remainingFields);
#endif
  if (!parsed) {
    return std::nullopt;
  }
  prepareParsedSong(*parsed);
  return parsed;
}

void SongLibrary::prepareParsedSong(MSong &song) const {
  evaluateComputedFieldsInSong(song, columnRegistry_);
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

  const std::optional<MSong> parsed =
      loadPreparedSongFromFile(path, remainingFields);
  // Keep stored metadata when the file cannot be read (for example moved or
  // deleted), instead of overwriting it with empty fields.
  if (!parsed) {
    return songs[songId];
  }
  syncBuiltInFieldsBySongId(songId, *parsed);
  syncDynamicAttributesBySongId(songId, *parsed);
  syncComputedValuesBySongId(songId, *parsed);
  return songs[songId];
}

std::optional<MSong>
SongLibrary::loadSongFromFile(const std::string &path) const {
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
  // Whether a source's key fields change, from the text before the update.
  std::vector<ProvidedSource *> rekeyed;
  for (auto &[sourceId, source] : providedSources_) {
    for (const std::string &keyField : source.provider.keyFields) {
      if (songFieldText(songs[songId], keyField) !=
          songFieldText(song, keyField)) {
        rekeyed.push_back(&source);
        break;
      }
    }
  }
  for (const QString &columnId : columns) {
    const std::string key = columnId.toStdString();
    const QString value = songFieldText(song, key);
    updateSong.bindValue(":" + columnId, value);

    songs[songId].insert_or_assign(key, FieldValue(value.toStdString(), key));
  }
  updateSong.bindValue(":song_id", songId);
  if (!updateSong.exec()) {
    qFatal("syncBuiltInFieldsBySongId failed: %s",
           updateSong.lastError().text().toUtf8().data());
  }
  // The only place a key changes: key fields are built-in tag fields. The
  // song loses the old key's values at once and gets the new key's at the
  // refresh; the old key's are not carried over, since other songs may keep
  // them (docs/plugins/playstats.md, "A re-tag starts fresh").
  for (ProvidedSource *source : rekeyed) {
    const QString oldKey = source->keys.keyBySongPk[songId];
    if (providedKey(source->provider, songs[songId]) == oldKey) {
      continue;
    }
    unindexProvidedKey(*source, songId);
    indexProvidedKey(*source, songId);
    setProvidedFields(source->provider, songs[songId],
                      toFieldValues(source->provider, nullptr));
    markStale(*source, source->keys.keyBySongPk[songId]);
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
