#ifndef PLAYLIST_H
#define PLAYLIST_H

#include "qlist.h"
#include "qurl.h"
class PlayList {
 public:
  virtual QList<int> getSourceIndices() = 0;
  virtual QUrl getUrl(int) = 0;
};

#endif  // PLAYLIST_H
