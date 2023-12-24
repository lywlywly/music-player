#ifndef PLAYERCONTROLMODEL_H
#define PLAYERCONTROLMODEL_H

#include <QObject>

#include "myproxymodel.h"
class PlayerControlModel : public QObject {
  Q_OBJECT
 public:
  explicit PlayerControlModel(MyProxyModel *parent = nullptr);
  enum PlaybackMode { RepeatPlayList, RepeatTrack, Random };
  QUrl getCurrentUrl();
  PlaybackMode playbackMode() const;
  void setPlaybackMode(PlaybackMode mode);
  int currentRawIndex() const;
  QUrl currentMedia() const;
  int nextIndex(int steps = 1) const;
  int previousIndex(int steps = 1) const;
 public slots:
  void shuffle();
  void next();
  void previous();
  void setCurrentIndex(int);
  void onPlayListChange();
 signals:
  // currently not used
  void indexChange();

 private:
  MyProxyModel *model;
  int index;
  int filePathColumnIndex;
  QList<int> indices;
};

#endif  // PLAYERCONTROLMODEL_H
