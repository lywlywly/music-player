#ifndef SONGSTORE_H
#define SONGSTORE_H

#include "songlibrary.h"
#include <QDebug>

// can duplicate
class SongStore
{
public:
  SongStore(SongLibrary &);
  int songCount() const;
  void addSong(MSong &&);
  // remove song by Pk
  void removeSongByPk(int);
  // remove song by index (current order)
  void removeSongByIndex(int);
  void clear();
  const MSong &getSongByPk(int) const;
  const MSong &getSongByIndex(int) const;
  const int getPkByIndex(int) const;
  const int getIndexByPk(int) const;
  void sortByField(std::string, int = 0);
  std::vector<int> query(std::string);
  std::vector<int> &queryView(std::string);
  std::vector<const std::string *> queryField(std::string);
  const std::vector<int> &getSongsView() const;
  const std::vector<int> &getIndices() const;

private:
  SongLibrary &library;
  std::vector<const MSong *> songs;
  std::vector<int> songPKs;
  std::vector<int> indices;
  // WrapperVec wrapper;
  // MSong *basePtr;
  // std::unordered_map<std::string, const MSong *> songs;
};

#endif // SONGSTORE_H
