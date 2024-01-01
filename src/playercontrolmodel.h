#ifndef PLAYERCONTROLMODEL_H
#define PLAYERCONTROLMODEL_H

#include <QObject>

#include "iplaylist.h"

class PlayerControlModel : public QObject {
  Q_OBJECT
 public:
  explicit PlayerControlModel(IPlayList *parent = nullptr);
  enum PlaybackMode { RepeatPlayList, RepeatTrack, Random };
  QUrl getCurrentUrl();
  PlaybackMode playbackMode() const;
  void setPlaybackMode(PlaybackMode mode);
  int currentRawIndex() const;
  QUrl currentMedia() const;
  int nextIndex(int steps = 1) const;
  int previousIndex(int steps = 1) const;
  void addToQueue(int);
  void setNext(int);
  QList<int> getQueue();
 public slots:
  void shuffle();
  void next();
  void previous();
  void setCurrentIndex(int);
  void onPlayListChange();
 signals:
  // currently not used
  void indexChange(int newIndex);

 private:
  void initPlayList();
  void handleInitialPlay();
  void moveIndex(int &index, bool isDirectionNext = true);
  IPlayList *model;
  int filePathColumnIndex;
  QList<int> indices;
  QList<int> playList = {};
  int position = -1;
  int toBeInsertedIndex = -1;
};

#endif  // PLAYERCONTROLMODEL_H
