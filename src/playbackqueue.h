#ifndef PLAYBACKQUEUE_H
#define PLAYBACKQUEUE_H

#include <QObject>
#include <deque>

class Playlist;

// Current song and queued songs
class PlaybackQueue : public QObject {
  Q_OBJECT
public:
  enum class PlaybackStatus { Playing, Paused, None };

  explicit PlaybackQueue(QObject *parent = nullptr);
  void setCurrentId(int, Playlist *pl);
  void addNext(int, Playlist *pl);
  void addLast(int, Playlist *pl);
  const std::deque<int> &getQueue();
  std::pair<int, Playlist *> getCurrentPk() const;
  void setPlaybackStatus(PlaybackStatus);
  PlaybackStatus getStatus();
  std::pair<int, Playlist *> pop();
  bool empty() const;
  using StatusUpdateCallback = std::function<void(int, Playlist *)>;
  void setStatusUpdateCallback(StatusUpdateCallback &&);
  std::pair<int, Playlist *> getOrder(int);

private:
  void notifyAll(int, Playlist *);
  std::deque<int> queue;
  std::vector<std::pair<int, Playlist *>> orders;
  int currentPk = -1;
  Playlist *currentPlaylist;
  PlaybackStatus playbackStatus = PlaybackStatus::None;
  std::vector<StatusUpdateCallback> cbs;

signals:
};

#endif // PLAYBACKQUEUE_H
