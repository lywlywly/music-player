#ifndef IPLAYLIST_H
#define IPLAYLIST_H

#include "qlist.h"
#include "qobject.h"
#include "qurl.h"
class IPlayList {
 public:
  virtual QList<int> getSourceIndices() = 0;
  virtual QUrl getUrl(int) = 0;

 signals:
  virtual void playlistChanged() = 0;
};
Q_DECLARE_INTERFACE(IPlayList, "IMyInterfaces");
#endif  // IPLAYLIST_H
