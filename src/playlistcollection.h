#ifndef PLAYLISTCOLLECTION_H
#define PLAYLISTCOLLECTION_H

#include "playlist.h"
#include <QObject>
#include <QString>
#include <map>
#include <memory>
#include <vector>

class DatabaseManager;

// Owns all playlists and the `playlists` table rows (insert, delete, name,
// tab_order). Handles playlist lifecycle (load, create, remove, rename,
// reorder) and cross-playlist notifications. Per-playlist operations are
// called on the Playlist returned by playlist(id).
class PlaylistCollection : public QObject {
  Q_OBJECT
public:
  PlaylistCollection(SongLibrary &, DatabaseManager &,
                     QObject *parent = nullptr);
  // Loads playlists ordered by tab_order (then playlist_id). If DB has no
  // playlist rows, creates the default playlist row (id=1). If tab_order is
  // corrupted (not 0..N-1), repairs order using playlist_id ascending.
  // Loads each Playlist's state. Emits no signals.
  void loadAll();

  std::vector<int> playlistIdsInOrder() const;
  // nullptr if unknown.
  Playlist *playlist(int playlistId) const;
  QString name(int playlistId) const;

  // Inserts a playlists row at the end of the order, creates and loads the
  // Playlist, emits playlistCreated, then appends songPks (repeats skipped).
  // Returns its id.
  int createPlaylist(const QString &name, const std::vector<int> &songPks = {});
  // Returns false for an unknown id, the only remaining playlist, or a failed
  // delete. Emits playlistAboutToBeRemoved before the Playlist is destroyed;
  // playlist_items rows cascade by FK.
  bool removePlaylist(int playlistId);
  // Trims name; returns false if empty or the update fails.
  bool renamePlaylist(int playlistId, const QString &name);
  // Writes contiguous tab_order (0..N-1) for the given playlist_id order.
  bool setPlaylistOrder(const std::vector<int> &orderedIds);

  // Emits row data-changed notifications for songPk in every playlist. Does
  // not mutate song data. No-op for songPk < 0.
  void notifySongChanged(int songPk);

signals:
  void playlistCreated(int playlistId);
  void playlistAboutToBeRemoved(int playlistId);
  void playlistRemoved(int playlistId);
  void playlistRenamed(int playlistId, const QString &name);

private:
  Playlist &addLoadedPlaylist(int playlistId, const QString &name);
  bool updatePlaylistTabOrder(const std::vector<int> &playlistIdsInOrder) const;
  SongLibrary &library_;
  DatabaseManager &databaseManager_;
  std::map<int, std::unique_ptr<Playlist>> playlists_;
  std::vector<int> order_;
  std::map<int, QString> names_;
};

#endif // PLAYLISTCOLLECTION_H
