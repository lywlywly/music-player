#include "playlist.h"
#include "databasemanager.h"
#include "fieldformatter.h"
#include <QSqlError>
#include <QSqlQuery>
#include <optional>
#include <stdexcept>
#include <unicode/coll.h>
#include <unicode/locid.h>
#include <unordered_set>

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
                                 ValueType valueType, bool ascending) {
  bool ok = false;
  const int cmp = compareFieldValues(a.field, b.field, valueType, ok);
  if (ok) {
    if (cmp == 0) {
      return false;
    }
    return ascending ? (cmp < 0) : (cmp > 0);
  }
  return std::nullopt;
}

SortValue resolveSortValue(const SongLibrary &library, int songPk,
                           const std::string &columnId) {
  const MSong &song = library.getSongByPK(songPk);
  auto it = song.find(columnId);
  if (it == song.end()) {
    return {true, FieldValue("", columnId)};
  }

  return {false, it->second};
}
} // namespace

Playlist::Playlist(int playlistId, SongLibrary &lib,
                   DatabaseManager &databaseManager, QObject *parent)
    : QObject(parent), playlistId_{playlistId}, library_{lib},
      databaseManager_{databaseManager} {}

bool Playlist::loadState() {
  if (playlistId_ <= 0) {
    return false;
  }

  QSqlDatabase &db = databaseManager_.db();
  QSqlQuery qPlaylist(db);
  qPlaylist.prepare(R"(
      SELECT last_played
      FROM playlists
      WHERE playlist_id=:playlist_id
  )");
  qPlaylist.bindValue(":playlist_id", playlistId_);
  if (!qPlaylist.exec() || !qPlaylist.next()) {
    return false;
  }
  lastPlayed_ = qPlaylist.value(0).toInt();

  songPKs_.clear();
  QSqlQuery qItems(db);
  qItems.prepare(R"(
      SELECT song_id, position
      FROM playlist_items
      WHERE playlist_id=:playlist_id
      ORDER BY position ASC
  )");
  qItems.bindValue(":playlist_id", playlistId_);
  if (!qItems.exec()) {
    return false;
  }
  while (qItems.next()) {
    songPKs_.push_back(qItems.value(0).toInt());
    nextPosition_ = qItems.value(1).toInt() + 1;
  }

  rebuildIndices();
  return true;
}

int Playlist::id() const { return playlistId_; }

int Playlist::songCount() const { return songPKs_.size(); }

bool Playlist::empty() const { return songCount() == 0; }

bool Playlist::containsPk(int pk) const {
  return pk >= 0 && pk < static_cast<int>(indices_.size()) && indices_[pk] >= 0;
}

const MSong &Playlist::getSongByPk(int pk) const {
  if (indices_[pk] == -1) {
    throw std::logic_error("Key not in view");
  }
  return library_.getSongByPK(pk);
}

const MSong &Playlist::getSongByIndex(int i) const {
  return library_.getSongByPK(songPKs_.at(i));
}

int Playlist::getIndexByPk(int pk) const {
  if (indices_[pk] == -1) {
    throw std::logic_error("Key not in view");
  }
  return indices_.at(pk);
}

int Playlist::getPkByIndex(int i) const { return songPKs_.at(i); }

int Playlist::getLastPlayed() const { return lastPlayed_; }

std::vector<int> Playlist::addFiles(const std::vector<std::string> &paths,
                                    const ProgressFn &progress,
                                    std::vector<std::string> *failedPaths) {
  std::vector<int> pks;
  pks.reserve(paths.size());
  std::unordered_set<int> seen;
  const int total = static_cast<int>(paths.size());
  for (int i = 0; i < total; ++i) {
    if (std::optional<MSong> song = library_.loadSongFromFile(paths[i])) {
      const int pk = library_.addTolibrary(std::move(*song));
      if (!containsPk(pk) && seen.insert(pk).second) {
        pks.push_back(pk);
      }
    } else if (failedPaths) {
      failedPaths->push_back(paths[i]);
    }
    if (progress) {
      progress(i + 1, total);
    }
  }
  if (pks.empty()) {
    return pks;
  }

  const int first = songCount();
  emit aboutToInsertSongs(first, first + static_cast<int>(pks.size()) - 1);
  for (int pk : pks) {
    appendSongPk(pk);
  }
  emit songsInserted(first, songCount() - 1);
  return pks;
}

bool Playlist::addSong(MSong &&s) {
  const int songId = library_.addTolibrary(std::move(s));
  return addSongByPk(songId);
}

bool Playlist::addSongByPk(int songId) {
  if (containsPk(songId)) {
    return false;
  }
  const int row = songCount();
  emit aboutToInsertSongs(row, row);
  appendSongPk(songId);
  emit songsInserted(row, row);
  return true;
}

void Playlist::removeSong(int row) {
  emit aboutToRemoveSongs(row, row);
  const int songId = songPKs_.at(row);
  songPKs_.erase(songPKs_.begin() + row);
  rebuildIndices();
  if (playlistId_ > 0) {
    removePlaylistItemInDb(songId);
  }
  emit songsRemoved(row, row);
}

void Playlist::clear() {
  emit aboutToResetSongs();
  songPKs_.clear();
  indices_.clear();
  if (playlistId_ > 0) {
    removePlaylistItemsInDb();
  }
  emit songsReset();
}

void Playlist::sortByField(const std::string &f, int order) {
  const bool ascending = (order == 0);

  std::stable_sort(songPKs_.begin(), songPKs_.end(), [&](int i, int j) {
    const SortValue valueA = resolveSortValue(library_, i, f);
    const SortValue valueB = resolveSortValue(library_, j, f);

    if (valueA.missing != valueB.missing) {
      // Missing values always last.
      return !valueA.missing;
    }
    if (valueA.missing && valueB.missing) {
      return false;
    }

    const ValueType valueType = valueA.field.valueType();
    Q_ASSERT(valueType == valueB.field.valueType());
    if (const std::optional<bool> typed =
            compareTyped(valueA, valueB, valueType, ascending);
        typed.has_value()) {
      return typed.value();
    }

    return compareText(valueA.field.text, valueB.field.text, ascending);
  });

  rebuildIndices();
  emit songsReordered();
}

void Playlist::refreshMetadataFromFiles(const ProgressFn &progress) {
  const int totalRows = songCount();
  std::vector<std::string> filepaths;
  filepaths.reserve(totalRows);

  for (int row = 0; row < totalRows; ++row) {
    const MSong &song = getSongByIndex(row);
    auto it = song.find("filepath");
    if (it == song.end()) {
      qFatal("refreshMetadataFromFiles: missing filepath at row=%d", row);
    }
    if (it->second.text.empty()) {
      qFatal("refreshMetadataFromFiles: empty filepath at row=%d", row);
    }
    filepaths.push_back(it->second.text);
  }

  emit aboutToResetSongs();
  library_.refreshSongsFromFilepaths(filepaths, progress);
  emit songsReset();
}

void Playlist::setLastPlayed(int songPk) {
  if (songPk < 0) {
    return;
  }
  lastPlayed_ = songPk;
  if (playlistId_ <= 0) {
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
  q.bindValue(":playlist_id", playlistId_);
  if (!q.exec()) {
    qFatal("setLastPlayed failed: %s", q.lastError().text().toUtf8().data());
  }
}

void Playlist::emitSongDataChangedBySongPk(int songPk) {
  if (songPk < 0) {
    return;
  }
  if (!containsPk(songPk)) {
    return;
  }
  emit songDataChanged(indices_[songPk]);
}

void Playlist::appendSongPk(int songId) {
  if (songId < 0) {
    qFatal("addSongByPk: invalid song id=%d", songId);
  }
  static_cast<void>(library_.getSongByPK(songId));
  songPKs_.push_back(songId);
  if (indices_.size() < songId + 1)
    indices_.resize(songId + 1, -1);
  indices_[songId] = songPKs_.size() - 1;
  if (playlistId_ > 0) {
    appendSongToPlaylistInDb(songId, nextPosition_++);
  }
}

void Playlist::appendSongToPlaylistInDb(int songId, int position) {
  QSqlDatabase &db = databaseManager_.db();
  QSqlQuery q(db);
  q.prepare(R"(
      INSERT INTO playlist_items(playlist_id, song_id, position)
      VALUES(:playlist_id, :song_id, :position)
  )");
  q.bindValue(":playlist_id", playlistId_);
  q.bindValue(":song_id", songId);
  q.bindValue(":position", position);
  if (!q.exec()) {
    qFatal("appendSongToPlaylistInDb failed: %s",
           q.lastError().text().toUtf8().data());
  }
}

void Playlist::removePlaylistItemInDb(int songId) {
  QSqlQuery q(databaseManager_.db());
  q.prepare(R"(
      DELETE FROM playlist_items
      WHERE playlist_id=:playlist_id AND song_id=:song_id
  )");
  q.bindValue(":playlist_id", playlistId_);
  q.bindValue(":song_id", songId);
  if (!q.exec()) {
    qFatal("removePlaylistItemInDb failed: %s",
           q.lastError().text().toUtf8().data());
  }
}

// Synchronous, so rows appended right after clear() are not deleted later.
void Playlist::removePlaylistItemsInDb() {
  QSqlQuery q(databaseManager_.db());
  q.prepare(R"(
      DELETE FROM playlist_items
      WHERE playlist_id=:playlist_id
  )");
  q.bindValue(":playlist_id", playlistId_);
  if (!q.exec()) {
    qFatal("removePlaylistItemsInDb failed: %s",
           q.lastError().text().toUtf8().data());
  }
}

void Playlist::rebuildIndices() {
  if (songPKs_.empty()) {
    indices_.clear();
    return;
  }

  int maxPk = 0;
  for (int pk : songPKs_)
    if (pk > maxPk)
      maxPk = pk;

  indices_.assign(maxPk + 1, -1);

  for (int row = 0; row < (int)songPKs_.size(); ++row) {
    int pk = songPKs_[row];
    indices_[pk] = row;
  }
}
