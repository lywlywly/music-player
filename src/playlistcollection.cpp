#include "playlistcollection.h"
#include "databasemanager.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>

namespace {
struct PlaylistDefinition {
  int playlistId = -1;
  QString name;
  int tabOrder = -1;
};
} // namespace

PlaylistCollection::PlaylistCollection(SongLibrary &library,
                                       DatabaseManager &databaseManager,
                                       QObject *parent)
    : QObject(parent), library_{library}, databaseManager_{databaseManager} {}

void PlaylistCollection::loadAll() {
  QSqlQuery q(databaseManager_.db());
  std::vector<PlaylistDefinition> definitions;
  if (!q.exec(R"(
        SELECT playlist_id, name, tab_order
        FROM playlists
        ORDER BY tab_order ASC, playlist_id ASC
    )")) {
    qWarning() << "PlaylistCollection::loadAll query failed:"
               << q.lastError().text();
    qFatal("PlaylistCollection::loadAll: no playlist definitions");
  }

  while (q.next()) {
    definitions.push_back(PlaylistDefinition{.playlistId = q.value(0).toInt(),
                                             .name = q.value(1).toString(),
                                             .tabOrder = q.value(2).toInt()});
  }
  if (definitions.empty()) {
    QSqlQuery ensureDefault(databaseManager_.db());
    if (!ensureDefault.exec(R"(
          INSERT INTO playlists(playlist_id, name, last_played, tab_order)
          VALUES(1, 'Default', 1, 0)
      )")) {
      qWarning() << "PlaylistCollection::loadAll ensure default failed:"
                 << ensureDefault.lastError().text();
      qFatal("PlaylistCollection::loadAll: no playlist definitions");
    }
    definitions.push_back(
        PlaylistDefinition{.playlistId = 1, .name = "Default", .tabOrder = 0});
  }

  bool tabOrderValid = true;
  for (int i = 0; i < static_cast<int>(definitions.size()); ++i) {
    if (definitions[i].tabOrder != i) {
      tabOrderValid = false;
      break;
    }
  }
  if (!tabOrderValid) {
    std::sort(definitions.begin(), definitions.end(),
              [](const PlaylistDefinition &a, const PlaylistDefinition &b) {
                return a.playlistId < b.playlistId;
              });
    std::vector<int> repairedOrder;
    repairedOrder.reserve(definitions.size());
    for (const PlaylistDefinition &definition : definitions) {
      repairedOrder.push_back(definition.playlistId);
    }
    if (!updatePlaylistTabOrder(repairedOrder)) {
      qWarning() << "PlaylistCollection::loadAll failed to repair tab_order";
      qFatal("PlaylistCollection::loadAll: no playlist definitions");
    }
  }

  for (const PlaylistDefinition &definition : definitions) {
    addLoadedPlaylist(definition.playlistId, definition.name);
  }
}

std::vector<int> PlaylistCollection::playlistIdsInOrder() const {
  return order_;
}

Playlist *PlaylistCollection::playlist(int playlistId) const {
  auto it = playlists_.find(playlistId);
  if (it == playlists_.end()) {
    return nullptr;
  }
  return it->second.get();
}

QString PlaylistCollection::name(int playlistId) const {
  auto it = names_.find(playlistId);
  if (it == names_.end()) {
    return {};
  }
  return it->second;
}

int PlaylistCollection::createPlaylist(const QString &name,
                                       const std::vector<int> &songPks) {
  QSqlQuery q(databaseManager_.db());
  q.prepare(R"(
      INSERT INTO playlists(name, last_played, tab_order)
      VALUES(:name, 1, :tab_order)
  )");
  q.bindValue(":name", name);
  q.bindValue(":tab_order", static_cast<int>(order_.size()));
  if (!q.exec()) {
    qFatal("PlaylistCollection::createPlaylist insert failed: %s",
           q.lastError().text().toUtf8().data());
  }
  const int playlistId = q.lastInsertId().toInt();

  Playlist &playlist = addLoadedPlaylist(playlistId, name);
  emit playlistCreated(playlistId);
  if (!updatePlaylistTabOrder(order_)) {
    qWarning()
        << "PlaylistCollection::createPlaylist failed to persist tab_order";
  }
  for (int songPk : songPks) {
    playlist.addSongByPk(songPk);
  }
  return playlistId;
}

bool PlaylistCollection::removePlaylist(int playlistId) {
  auto it = playlists_.find(playlistId);
  if (it == playlists_.end() || playlists_.size() <= 1) {
    return false;
  }
  if (playlistId <= 0) {
    return false;
  }

  QSqlQuery q(databaseManager_.db());
  q.prepare(R"(
      DELETE FROM playlists
      WHERE playlist_id=:playlist_id
  )");
  q.bindValue(":playlist_id", playlistId);
  if (!q.exec()) {
    qWarning() << "PlaylistCollection::removePlaylist delete failed:"
               << q.lastError().text();
    return false;
  }

  emit playlistAboutToBeRemoved(playlistId);
  order_.erase(std::remove(order_.begin(), order_.end(), playlistId),
               order_.end());
  names_.erase(playlistId);
  playlists_.erase(it);
  if (!updatePlaylistTabOrder(order_)) {
    qWarning()
        << "PlaylistCollection::removePlaylist failed to persist tab_order";
  }
  emit playlistRemoved(playlistId);
  return true;
}

bool PlaylistCollection::renamePlaylist(int playlistId, const QString &name) {
  const QString trimmedName = name.trimmed();
  if (trimmedName.isEmpty() || playlistId <= 0) {
    return false;
  }

  QSqlQuery q(databaseManager_.db());
  q.prepare(R"(
      UPDATE playlists
      SET name=:name
      WHERE playlist_id=:playlist_id
  )");
  q.bindValue(":name", trimmedName);
  q.bindValue(":playlist_id", playlistId);
  if (!q.exec()) {
    qWarning() << "PlaylistCollection::renamePlaylist update failed:"
               << q.lastError().text();
    return false;
  }
  if (names_.contains(playlistId)) {
    names_[playlistId] = trimmedName;
  }
  emit playlistRenamed(playlistId, trimmedName);
  return true;
}

bool PlaylistCollection::setPlaylistOrder(const std::vector<int> &orderedIds) {
  order_ = orderedIds;
  return updatePlaylistTabOrder(order_);
}

void PlaylistCollection::notifySongChanged(int songPk) {
  if (songPk < 0) {
    return;
  }
  for (auto &entry : playlists_) {
    entry.second->emitSongDataChangedBySongPk(songPk);
  }
}

Playlist &PlaylistCollection::addLoadedPlaylist(int playlistId,
                                                const QString &name) {
  auto playlist =
      std::make_unique<Playlist>(playlistId, library_, databaseManager_);
  if (!playlist->loadState()) {
    qFatal(
        "PlaylistCollection::addLoadedPlaylist: failed to load playlist state");
  }

  auto [it, inserted] = playlists_.try_emplace(playlistId, std::move(playlist));
  if (!inserted) {
    qFatal("PlaylistCollection::addLoadedPlaylist: duplicate playlist id");
  }
  order_.push_back(playlistId);
  names_[playlistId] = name;
  return *it->second;
}

bool PlaylistCollection::updatePlaylistTabOrder(
    const std::vector<int> &playlistIdsInOrder) const {
  if (playlistIdsInOrder.empty()) {
    return true;
  }

  QSqlDatabase &db = databaseManager_.db();
  if (!db.transaction()) {
    qWarning() << "PlaylistCollection::updatePlaylistTabOrder: failed to start "
                  "transaction";
    return false;
  }

  QSqlQuery q(db);
  q.prepare(R"(
      UPDATE playlists
      SET tab_order=:tab_order
      WHERE playlist_id=:playlist_id
  )");
  for (int i = 0; i < static_cast<int>(playlistIdsInOrder.size()); ++i) {
    q.bindValue(":tab_order", i);
    q.bindValue(":playlist_id", playlistIdsInOrder[i]);
    if (!q.exec()) {
      db.rollback();
      qWarning() << "PlaylistCollection::updatePlaylistTabOrder update failed:"
                 << q.lastError().text();
      return false;
    }
  }
  if (!db.commit()) {
    db.rollback();
    qWarning() << "PlaylistCollection::updatePlaylistTabOrder commit failed:"
               << db.lastError();
    return false;
  }
  return true;
}
