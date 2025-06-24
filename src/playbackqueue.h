#ifndef PLAYBACKQUEUE_H
#define PLAYBACKQUEUE_H

#include <QObject>
#include <deque>

class Playlist;

class PlaybackQueue : public QObject {
  Q_OBJECT
public:
  explicit PlaybackQueue(QObject *parent = nullptr);
  void setCurrentId(int, Playlist *pl);
  void addNext(int, Playlist *pl);
  void addLast(int, Playlist *pl);
  const std::deque<int> &getQueue();
  std::pair<int, Playlist *> getCurrentPk() const;
  int getStatus();
  std::pair<int, Playlist *> pop();
  bool empty() const;
  using statusUpdateCallback = std::function<void(int, Playlist *)>;
  void setStatusUpdateCallback(statusUpdateCallback &&);
  std::pair<int, Playlist *> getOrder(int);

private:
  void notifyAll(int, Playlist *);
  std::deque<int> queue;
  std::vector<std::pair<int, Playlist *>> orders;
  int currentPk = -1;
  Playlist *currentPlaylist;
  int status; // play/pause
  std::vector<statusUpdateCallback> cbs;
  // statusUpdateCallback cb;

signals:
};

#endif // PLAYBACKQUEUE_H
