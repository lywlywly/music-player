#include "playlistcollection.h"
#include "databasemanager.h"
#include "monitoredlibrary.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>
#include <iterator>

namespace {
struct PlaylistDefinition {
  int playlistId = -1;
  QString name;
  int tabOrder = -1;
  Playlist::Kind kind = Playlist::Kind::Manual;
  QString expression;
};

// An autoplaylist's expression: a null QString would bind as NULL, which the
// playlists CHECK rejects.
QString storedExpression(const QString &expression) {
  return expression.isNull() ? QStringLiteral("") : expression;
}
} // namespace

PlaylistCollection::PlaylistCollection(SongLibrary &library,
                                       DatabaseManager &databaseManager,
                                       MonitoredLibrary &monitoredLibrary,
                                       QObject *parent)
    : QObject(parent), library_{library}, databaseManager_{databaseManager},
      monitoredLibrary_{monitoredLibrary} {
  // Entered songs may be existing rows refreshed from their file.
  connect(&monitoredLibrary_, &MonitoredLibrary::songsEntered, this,
          &PlaylistCollection::notifySongsChanged);
  connect(&monitoredLibrary_, &MonitoredLibrary::songsUpdated, this,
          &PlaylistCollection::notifySongsChanged);
  // Views first, then delete.
  connect(&monitoredLibrary_, &MonitoredLibrary::songsLeft, this,
          [this](const std::vector<int> &songPks) {
            updateAutoPlaylistRows(songPks);
            releaseSongs(songPks);
          });
}

void PlaylistCollection::loadAll() {
  QSqlQuery q(databaseManager_.db());
  std::vector<PlaylistDefinition> definitions;
  if (!q.exec(R"(
        SELECT playlist_id, name, tab_order, kind, expression
        FROM playlists
        ORDER BY tab_order ASC, playlist_id ASC
    )")) {
    qWarning() << "PlaylistCollection::loadAll query failed:"
               << q.lastError().text();
    qFatal("PlaylistCollection::loadAll: no playlist definitions");
  }

  while (q.next()) {
    definitions.push_back(PlaylistDefinition{
        .playlistId = q.value(0).toInt(),
        .name = q.value(1).toString(),
        .tabOrder = q.value(2).toInt(),
        .kind = q.value(3).toString() == QLatin1String("auto")
                    ? Playlist::Kind::Auto
                    : Playlist::Kind::Manual,
        .expression = q.value(4).toString()});
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
    addLoadedPlaylist(definition.playlistId, definition.name, definition.kind);
    if (definition.kind == Playlist::Kind::Auto) {
      setAutoPlaylistFilter(definition.playlistId, definition.expression);
      fillAutoPlaylist(definition.playlistId);
    }
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
  const int playlistId = insertPlaylistRow(name, Playlist::Kind::Manual);
  Playlist &playlist =
      addLoadedPlaylist(playlistId, name, Playlist::Kind::Manual);
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

int PlaylistCollection::createAutoPlaylist(const QString &name,
                                           const QString &expression) {
  const int playlistId =
      insertPlaylistRow(name, Playlist::Kind::Auto, expression);
  addLoadedPlaylist(playlistId, name, Playlist::Kind::Auto);
  setAutoPlaylistFilter(playlistId, expression);
  emit playlistCreated(playlistId);
  if (!updatePlaylistTabOrder(order_)) {
    qWarning()
        << "PlaylistCollection::createAutoPlaylist failed to persist tab_order";
  }
  fillAutoPlaylist(playlistId);
  return playlistId;
}

int PlaylistCollection::insertPlaylistRow(const QString &name,
                                          Playlist::Kind kind,
                                          const QString &expression) {
  const bool isAutoKind = kind == Playlist::Kind::Auto;
  QSqlQuery q(databaseManager_.db());
  q.prepare(R"(
      INSERT INTO playlists(name, last_played, tab_order, kind, expression)
      VALUES(:name, 1, :tab_order, :kind, :expression)
  )");
  q.bindValue(":name", name);
  q.bindValue(":tab_order", static_cast<int>(order_.size()));
  q.bindValue(":kind", isAutoKind ? QStringLiteral("auto")
                                  : QStringLiteral("manual"));
  q.bindValue(":expression",
              isAutoKind ? QVariant(storedExpression(expression)) : QVariant());
  if (!q.exec()) {
    qFatal("PlaylistCollection::insertPlaylistRow failed: %s",
           q.lastError().text().toUtf8().data());
  }
  return q.lastInsertId().toInt();
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

  std::vector<int> songPks;
  if (it->second->kind() == Playlist::Kind::Manual) {
    songPks.reserve(it->second->songCount());
    for (int row = 0; row < it->second->songCount(); ++row) {
      songPks.push_back(it->second->getPkByIndex(row));
    }
  }
  emit playlistAboutToBeRemoved(playlistId);
  order_.erase(std::remove(order_.begin(), order_.end(), playlistId),
               order_.end());
  names_.erase(playlistId);
  autoPlaylists_.erase(playlistId);
  playlists_.erase(it);
  if (!updatePlaylistTabOrder(order_)) {
    qWarning()
        << "PlaylistCollection::removePlaylist failed to persist tab_order";
  }
  emit playlistRemoved(playlistId);
  releaseSongs(songPks);
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

bool PlaylistCollection::isAuto(int playlistId) const {
  return autoPlaylists_.contains(playlistId);
}

QString PlaylistCollection::autoPlaylistExpression(int playlistId) const {
  const auto it = autoPlaylists_.find(playlistId);
  return it == autoPlaylists_.end() ? QString() : it->second.expression;
}

QString PlaylistCollection::autoPlaylistError(int playlistId) const {
  const auto it = autoPlaylists_.find(playlistId);
  return it == autoPlaylists_.end() ? QString() : it->second.error;
}

bool PlaylistCollection::setAutoPlaylistExpression(int playlistId,
                                                   const QString &expression) {
  if (!isAuto(playlistId)) {
    return false;
  }
  QSqlQuery q(databaseManager_.db());
  q.prepare(R"(
      UPDATE playlists
      SET expression=:expression
      WHERE playlist_id=:playlist_id
  )");
  q.bindValue(":expression", storedExpression(expression));
  q.bindValue(":playlist_id", playlistId);
  if (!q.exec()) {
    qWarning() << "PlaylistCollection::setAutoPlaylistExpression update failed:"
               << q.lastError().text();
    return false;
  }
  setAutoPlaylistFilter(playlistId, expression);
  fillAutoPlaylist(playlistId);
  return true;
}

void PlaylistCollection::reloadAutoPlaylistExpressions() {
  for (auto &[playlistId, autoPlaylist] : autoPlaylists_) {
    setAutoPlaylistFilter(playlistId, autoPlaylist.expression);
    fillAutoPlaylist(playlistId);
  }
}

void PlaylistCollection::notifySongChanged(int songPk) {
  notifySongsChanged({songPk});
}

void PlaylistCollection::notifySongsChanged(const std::vector<int> &songPks) {
  std::vector<int> valid;
  std::copy_if(songPks.begin(), songPks.end(), std::back_inserter(valid),
               [](int songPk) { return songPk >= 0; });
  updateAutoPlaylistRows(valid);
  for (int songPk : valid) {
    for (auto &entry : playlists_) {
      entry.second->emitSongDataChangedBySongPk(songPk);
    }
  }
}

MonitoredLibrary &PlaylistCollection::monitoredLibrary() const {
  return monitoredLibrary_;
}

Playlist &PlaylistCollection::addLoadedPlaylist(int playlistId,
                                                const QString &name,
                                                Playlist::Kind kind) {
  auto playlist =
      std::make_unique<Playlist>(playlistId, library_, databaseManager_, kind);
  if (!playlist->loadState()) {
    qFatal(
        "PlaylistCollection::addLoadedPlaylist: failed to load playlist state");
  }

  auto [it, inserted] = playlists_.try_emplace(playlistId, std::move(playlist));
  if (!inserted) {
    qFatal("PlaylistCollection::addLoadedPlaylist: duplicate playlist id");
  }
  connect(it->second.get(), &Playlist::songPksRemoved, this,
          &PlaylistCollection::releaseSongs);
  connect(it->second.get(), &Playlist::songsRefreshed, this,
          &PlaylistCollection::notifySongsChanged);
  order_.push_back(playlistId);
  names_[playlistId] = name;
  return *it->second;
}

void PlaylistCollection::setAutoPlaylistFilter(int playlistId,
                                               const QString &expression) {
  ExprParseResult parsed = parseSongFilter(library_, expression);
  autoPlaylists_[playlistId] = AutoPlaylist{.expression = expression,
                                            .filter = std::move(parsed.expr),
                                            .error = parsed.error.message};
}

bool PlaylistCollection::isAutoPlaylistMember(const AutoPlaylist &autoPlaylist,
                                              int songPk) const {
  return autoPlaylist.filter && monitoredLibrary_.holds(songPk) &&
         library_.matches(*autoPlaylist.filter, songPk);
}

void PlaylistCollection::fillAutoPlaylist(int playlistId) {
  const AutoPlaylist &autoPlaylist = autoPlaylists_.at(playlistId);
  std::vector<int> members;
  for (int songPk : monitoredLibrary_.songPks()) {
    if (isAutoPlaylistMember(autoPlaylist, songPk)) {
      members.push_back(songPk);
    }
  }
  std::sort(members.begin(), members.end(), [this](int a, int b) {
    return library_.getSongByPK(a).at("filepath").text <
           library_.getSongByPK(b).at("filepath").text;
  });
  playlists_.at(playlistId)->resetRows(members);
}

void PlaylistCollection::updateAutoPlaylistRows(
    const std::vector<int> &songPks) {
  for (const auto &[playlistId, autoPlaylist] : autoPlaylists_) {
    Playlist &playlist = *playlists_.at(playlistId);
    std::vector<int> entering;
    std::vector<int> leaving;
    for (int songPk : songPks) {
      const bool member = isAutoPlaylistMember(autoPlaylist, songPk);
      if (member && !playlist.containsPk(songPk)) {
        entering.push_back(songPk);
      } else if (!member && playlist.containsPk(songPk)) {
        leaving.push_back(songPk);
      }
    }
    playlist.removeRowsByPk(leaving);
    playlist.appendRows(entering);
  }
}

void PlaylistCollection::releaseSongs(const std::vector<int> &songPks) {
  std::vector<int> unused;
  std::copy_if(
      songPks.begin(), songPks.end(), std::back_inserter(unused),
      [this](int songPk) {
        return !monitoredLibrary_.holds(songPk) &&
               std::none_of(playlists_.begin(), playlists_.end(),
                            [songPk](const auto &entry) {
                              return entry.second->kind() ==
                                         Playlist::Kind::Manual &&
                                     entry.second->containsPk(songPk);
                            });
      });
  if (unused.empty()) {
    return;
  }
  library_.removeSongs(unused);
  emit songsRemovedFromLibrary(unused);
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
