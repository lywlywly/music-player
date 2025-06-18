#ifndef PLAYBACKQUEUE_H
#define PLAYBACKQUEUE_H

#include <QObject>
#include <deque>

class PlaybackQueue : public QObject {
  Q_OBJECT
public:
  explicit PlaybackQueue(QObject *parent = nullptr);
  void setCurrentId(int);
  void addNext(int);
  void addLast(int);
  const std::deque<int> &getQueue();
  int getCurrentPk() const;
  int getStatus();
  int pop();
  bool empty() const;
  using statusUpdateCallback = std::function<void(int)>;
  void setStatusUpdateCallback(statusUpdateCallback &&);
  int getOrder(int);

private:
  std::deque<int> queue;
  std::vector<int> orders;
  int currentId = -1;
  int status; // play/pause
  statusUpdateCallback cb;

signals:
};

#endif // PLAYBACKQUEUE_H
