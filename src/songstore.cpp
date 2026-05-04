#include "songstore.h"
#include "databasemanager.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QtConcurrent>
#include <optional>
#include <stdexcept>
#include <unicode/coll.h>
#include <unicode/locid.h>

UErrorCode status = U_ZERO_ERROR;
icu::Locale loc = icu::Locale::forLanguageTag("zh-u-kr-latn-hani-hrkt", status);
// ordering based on simplified chinese pinyin, default ordering for other
// characters. Keep latin letters first, then Han (Hanzi, Kanji, Hanja), then
// Hiragana and Katakana
std::unique_ptr<icu::Collator> collator(icu::Collator::createInstance(loc,
                                                                      status));

namespace {
struct SortValue {
  bool missing = false;
  FieldValue field;
};

bool compareText(const std::string &a, const std::string &b, bool ascending) {
  icu::UnicodeString ua = icu::UnicodeString::fromUTF8(a);
  icu::UnicodeString ub = icu::UnicodeString::fromUTF8(b);
  const UCollationResult cmp =
      static_cast<UCollationResult>(collator->compare(ua, ub));
  return ascending ? (cmp == UCOL_LESS) : (cmp == UCOL_GREATER);
}

std::optional<bool> compareTyped(const SortValue &a, const SortValue &b,
                                 bool ascending) {
  if (a.field.type != b.field.type) {
    return std::nullopt;
  }

  if (a.field.type == ColumnValueType::Number) {
    if (a.field.typed.number == b.field.typed.number) {
      return false;
    }
    return ascending ? (a.field.typed.number < b.field.typed.number)
                     : (a.field.typed.number > b.field.typed.number);
  }

  if (a.field.type == ColumnValueType::DateTime) {
    if (a.field.typed.epochMs == b.field.typed.epochMs) {
      return false;
    }
    return ascending ? (a.field.typed.epochMs < b.field.typed.epochMs)
                     : (a.field.typed.epochMs > b.field.typed.epochMs);
  }

  if (a.field.type == ColumnValueType::Boolean) {
    if (a.field.typed.boolean == b.field.typed.boolean) {
      return false;
    }
    return ascending ? (a.field.typed.boolean < b.field.typed.boolean)
                     : (a.field.typed.boolean > b.field.typed.boolean);
  }

  return std::nullopt;
}

SortValue resolveSortValue(const SongLibrary &library, int songPk,
                           const std::string &columnId) {
  const MSong &song = library.getSongByPK(songPk);
  auto it = song.find(columnId);
  if (it == song.end()) {
    return {true, FieldValue{}};
  }

  return {false, it->second};
}
} // namespace

SongStore::SongStore(SongLibrary &lib, DatabaseManager &databaseManager,
                     int pid)
    : playlistId{pid}, library{lib}, databaseManager_{databaseManager} {}

int SongStore::songCount() const { return songPKs.size(); }

void SongStore::addSong(MSong &&s) {
  const int songId = library.addTolibrary(std::move(s));
  addSongByPk(songId);
}

void SongStore::addSongByPk(int songId) {
  if (songId < 0) {
    qFatal("addSongByPk: invalid song id=%d", songId);
  }
  static_cast<void>(library.getSongByPK(songId));
  songPKs.push_back(songId);
  if (indices.size() < songId + 1)
    indices.resize(songId + 1, -1);
  indices[songId] = songPKs.size() - 1;
  if (playlistId > 0) {
    appendSongToPlaylistInDb(songId, songPKs.size());
  }
}

void SongStore::removeSongByPk(int pk) { removeSongByIndex(indices.at(pk)); }

void SongStore::removeSongByIndex(int i) {
  songPKs.erase(songPKs.begin() + i);
  rebuildIndices();
}

void SongStore::clear() {
  songPKs.clear();
  indices.clear();
  if (playlistId > 0) {
    removePlaylistItemsInDb();
  }
}

const MSong &SongStore::getSongByPk(int pk) const {
  if (indices[pk] == -1) {
    throw std::logic_error("Key not in view");
  }
  return library.getSongByPK(pk);
}

const MSong &SongStore::getSongByIndex(int i) const {
  return library.getSongByPK(songPKs.at(i));
}

int SongStore::getPkByIndex(int i) const { return songPKs.at(i); }

int SongStore::getIndexByPk(int pk) const {
  if (indices[pk] == -1) {
    throw std::logic_error("Key not in view");
  }
  return indices.at(pk);
}

bool SongStore::containsPk(int pk) const {
  return pk >= 0 && pk < static_cast<int>(indices.size()) && indices[pk] >= 0;
}

void SongStore::sortByField(std::string f, int order) {
  const bool ascending = (order == 0);

  std::stable_sort(songPKs.begin(), songPKs.end(), [&](int i, int j) {
    const SortValue valueA = resolveSortValue(library, i, f);
    const SortValue valueB = resolveSortValue(library, j, f);

    if (valueA.missing != valueB.missing) {
      // Missing values always last.
      return !valueA.missing;
    }
    if (valueA.missing && valueB.missing) {
      return false;
    }

    if (const std::optional<bool> typed =
            compareTyped(valueA, valueB, ascending);
        typed.has_value()) {
      return typed.value();
    }

    return compareText(valueA.field.text, valueB.field.text, ascending);
  });

  rebuildIndices();
}

const std::vector<int> &SongStore::getSongsView() const { return songPKs; }

const std::vector<int> &SongStore::getIndices() const { return indices; }

void SongStore::setLastPlayed(int songPk) {
  if (playlistId <= 0 || songPk < 0) {
    return;
  }

  QSqlDatabase &db = databaseManager_.db();
  QSqlQuery q(db);
  q.prepare(R"(
      UPDATE playlists
      SET last_played=:last_played
      WHERE playlist_id=:playlist_id
  )");
  q.bindValue(":last_played", songPk);
  q.bindValue(":playlist_id", playlistId);
  if (!q.exec()) {
    qFatal("setLastPlayed failed: %s", q.lastError().text().toUtf8().data());
  }
}

void SongStore::refreshSongsFromFilepaths(
    const std::vector<std::string> &filepaths,
    const std::function<void(int current, int total)> &progressCallback) {
  library.refreshSongsFromFilepaths(filepaths, progressCallback);
}

bool SongStore::loadPlaylistState(int &lastPlayed) {
  if (playlistId <= 0) {
    return false;
  }

  QSqlDatabase &db = databaseManager_.db();
  QSqlQuery qPlaylist(db);
  qPlaylist.prepare(R"(
      SELECT last_played
      FROM playlists
      WHERE playlist_id=:playlist_id
  )");
  qPlaylist.bindValue(":playlist_id", playlistId);
  if (!qPlaylist.exec() || !qPlaylist.next()) {
    return false;
  }
  lastPlayed = qPlaylist.value(0).toInt();

  songPKs.clear();
  QSqlQuery qItems(db);
  qItems.prepare(R"(
      SELECT song_id
      FROM playlist_items
      WHERE playlist_id=:playlist_id
      ORDER BY position ASC
  )");
  qItems.bindValue(":playlist_id", playlistId);
  if (!qItems.exec()) {
    return false;
  }
  while (qItems.next()) {
    songPKs.push_back(qItems.value(0).toInt());
  }

  rebuildIndices();
  return true;
}

void SongStore::appendSongToPlaylistInDb(int songId, int position) {
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

void SongStore::removePlaylistItemsInDb() {
  if (playlistId <= 0) {
    return;
  }

  QSqlDatabase &mainDb = databaseManager_.db();
  const QString dbPath = mainDb.databaseName();
  if (dbPath.isEmpty() || dbPath == ":memory:") {
    QSqlQuery q(mainDb);
    q.prepare(R"(
        DELETE FROM playlist_items
        WHERE playlist_id=:playlist_id
    )");
    q.bindValue(":playlist_id", playlistId);
    if (!q.exec()) {
      qFatal("removePlaylistItemsInDb failed: %s",
             q.lastError().text().toUtf8().data());
    }
    return;
  }

  const int targetPlaylistId = playlistId;
  QtConcurrent::run([dbPath, targetPlaylistId]() {
    const QString connectionName =
        QStringLiteral("remove_playlist_items_%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
      qWarning() << "removePlaylistItemsInDb worker open failed:"
                 << db.lastError().text();
      db = QSqlDatabase();
      QSqlDatabase::removeDatabase(connectionName);
      return;
    }

    QSqlQuery q(db);
    q.prepare(R"(
        DELETE FROM playlist_items
        WHERE playlist_id=:playlist_id
    )");
    q.bindValue(":playlist_id", targetPlaylistId);
    if (!q.exec()) {
      qWarning() << "removePlaylistItemsInDb worker delete failed:"
                 << q.lastError().text();
    }

    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  });
}

void SongStore::rebuildIndices() {
  if (songPKs.empty()) {
    indices.clear();
    return;
  }

  int maxPk = 0;
  for (int pk : songPKs)
    if (pk > maxPk)
      maxPk = pk;

  indices.assign(maxPk + 1, -1);

  for (int row = 0; row < (int)songPKs.size(); ++row) {
    int pk = songPKs[row];
    indices[pk] = row;
  }
}
