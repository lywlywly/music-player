#ifndef PLAYLISTCOLLECTION_H
#define PLAYLISTCOLLECTION_H

#include "playlist.h"
#include <QObject>
#include <QString>
#include <map>
#include <memory>
#include <vector>

class DatabaseManager;
class MonitoredLibrary;

// Owns all playlists and the `playlists` table rows (insert, delete, name,
// tab_order). Handles playlist lifecycle (load, create, remove, rename,
// reorder) and cross-playlist notifications. Per-playlist operations are
// called on the Playlist returned by playlist(id).
// Song lifetime: a song row lives while the manual library (a playlist) or
// the monitored library holds it. When either lets go of songs (a song leaves
// its last playlist through Playlist::removeSongs, clear or removePlaylist, or
// leaves the monitored library), releaseSongs removes from SongLibrary those
// the other does not hold; see songsRemovedFromLibrary. It is the only place
// that deletes songs.
class PlaylistCollection : public QObject {
  Q_OBJECT
public:
  PlaylistCollection(SongLibrary &, DatabaseManager &, MonitoredLibrary &,
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
  // playlist_items rows cascade by FK. Then releases its songs (see
  // releaseSongs).
  bool removePlaylist(int playlistId);
  // Trims name; returns false if empty or the update fails.
  bool renamePlaylist(int playlistId, const QString &name);
  // Writes contiguous tab_order (0..N-1) for the given playlist_id order.
  bool setPlaylistOrder(const std::vector<int> &orderedIds);

  // Emits row data-changed notifications for songPk in every playlist. Does
  // not mutate song data. No-op for songPk < 0.
  void notifySongChanged(int songPk);

  MonitoredLibrary &monitoredLibrary() const;

signals:
  void playlistCreated(int playlistId);
  void playlistAboutToBeRemoved(int playlistId);
  void playlistRemoved(int playlistId);
  void playlistRenamed(int playlistId, const QString &name);
  // The songs were removed from SongLibrary, as neither library holds them
  // any more.
  void songsRemovedFromLibrary(const std::vector<int> &songPks);

private:
  Playlist &addLoadedPlaylist(int playlistId, const QString &name);
  bool updatePlaylistTabOrder(const std::vector<int> &playlistIdsInOrder) const;
  // Removes from SongLibrary the songs of songPks that are in no playlist and
  // not held by the monitored library, then emits songsRemovedFromLibrary.
  void releaseSongs(const std::vector<int> &songPks);
  void notifySongsChanged(const std::vector<int> &songPks);
  SongLibrary &library_;
  DatabaseManager &databaseManager_;
  MonitoredLibrary &monitoredLibrary_;
  std::map<int, std::unique_ptr<Playlist>> playlists_;
  std::vector<int> order_;
  std::map<int, QString> names_;
};

#endif // PLAYLISTCOLLECTION_H
