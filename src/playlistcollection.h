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
// Autoplaylists (Playlist::Kind::Auto) hold no songs: their rows are the
// monitored songs matching their expression (parseSongFilter), in filepath
// order when filled, with songs that start matching appended. They are kept
// current from the monitored library's signals, notifySongsChanged and
// Playlist::songsRefreshed. An invalid expression gives no rows and an error
// (autoPlaylistError); its text is kept.
class PlaylistCollection : public QObject {
  Q_OBJECT
public:
  PlaylistCollection(SongLibrary &, DatabaseManager &, MonitoredLibrary &,
                     QObject *parent = nullptr);
  // Loads playlists ordered by tab_order (then playlist_id). If DB has no
  // playlist rows, creates the default playlist row (id=1). If tab_order is
  // corrupted (not 0..N-1), repairs order using playlist_id ascending.
  // Loads each Playlist's state and fills autoplaylists from the loaded
  // monitored library. Emits no signals.
  void loadAll();

  std::vector<int> playlistIdsInOrder() const;
  // nullptr if unknown.
  Playlist *playlist(int playlistId) const;
  QString name(int playlistId) const;

  // Inserts a playlists row at the end of the order, creates and loads the
  // Playlist, emits playlistCreated, then appends songPks (repeats skipped).
  // Returns its id.
  int createPlaylist(const QString &name, const std::vector<int> &songPks = {});
  // Like createPlaylist, for an autoplaylist filled from expression (kept
  // even if invalid).
  int createAutoPlaylist(const QString &name, const QString &expression);
  // Returns false for an unknown id, the only remaining playlist, or a failed
  // delete. Emits playlistAboutToBeRemoved before the Playlist is destroyed;
  // playlist_items rows cascade by FK. Then releases its songs (see
  // releaseSongs); an autoplaylist releases none.
  bool removePlaylist(int playlistId);
  // Trims name; returns false if empty or the update fails.
  bool renamePlaylist(int playlistId, const QString &name);
  // Writes contiguous tab_order (0..N-1) for the given playlist_id order.
  bool setPlaylistOrder(const std::vector<int> &orderedIds);

  bool isAuto(int playlistId) const;
  // Empty for an unknown id or a manual playlist.
  QString autoPlaylistExpression(int playlistId) const;
  // Why the autoplaylist's expression is invalid; empty when it is valid, or
  // for an unknown id or a manual playlist.
  QString autoPlaylistError(int playlistId) const;
  // Stores the expression (even if invalid), refills the rows in filepath
  // order and emits autoPlaylistExpressionChanged. Returns false for an unknown
  // id, a manual playlist, or a failed update.
  bool setAutoPlaylistExpression(int playlistId, const QString &expression);
  // Re-parses every autoplaylist expression and refills the rows, after
  // custom fields changed; emits autoPlaylistExpressionChanged for each.
  void reloadAutoPlaylistExpressions();

  // After song data changed (tags, play stats): updates autoplaylist rows,
  // then emits row data-changed notifications in every playlist. Does not
  // mutate song data. Negative pks are ignored.
  void notifySongChanged(int songPk);
  void notifySongsChanged(const std::vector<int> &songPks);

  MonitoredLibrary &monitoredLibrary() const;

signals:
  void playlistCreated(int playlistId);
  void playlistAboutToBeRemoved(int playlistId);
  void playlistRemoved(int playlistId);
  void playlistRenamed(int playlistId, const QString &name);
  // After setAutoPlaylistExpression or reloadAutoPlaylistExpressions: the
  // expression or its error (autoPlaylistError) may have changed.
  void autoPlaylistExpressionChanged(int playlistId);
  // The songs were removed from SongLibrary, as neither library holds them
  // any more.
  void songsRemovedFromLibrary(const std::vector<int> &songPks);

private:
  struct AutoPlaylist {
    QString expression;
    // Null when the expression is invalid.
    ExprPtr filter;
    QString error;
  };
  // expression is stored for an autoplaylist only.
  int insertPlaylistRow(const QString &name, Playlist::Kind kind,
                        const QString &expression = {});
  Playlist &addLoadedPlaylist(int playlistId, const QString &name,
                              Playlist::Kind kind);
  void setAutoPlaylistFilter(int playlistId, const QString &expression);
  bool isAutoPlaylistMember(const AutoPlaylist &, int songPk) const;
  // Sets the rows to the matching monitored songs, in filepath order.
  void fillAutoPlaylist(int playlistId);
  // Adds and removes the songs in every autoplaylist by whether they match.
  void updateAutoPlaylistRows(const std::vector<int> &songPks);
  bool updatePlaylistTabOrder(const std::vector<int> &playlistIdsInOrder) const;
  // Removes from SongLibrary the songs of songPks that are in no manual
  // playlist and not held by the monitored library, then emits
  // songsRemovedFromLibrary.
  void releaseSongs(const std::vector<int> &songPks);
  SongLibrary &library_;
  DatabaseManager &databaseManager_;
  MonitoredLibrary &monitoredLibrary_;
  std::map<int, std::unique_ptr<Playlist>> playlists_;
  std::vector<int> order_;
  std::map<int, QString> names_;
  std::map<int, AutoPlaylist> autoPlaylists_;
};

#endif // PLAYLISTCOLLECTION_H
