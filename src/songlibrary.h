#ifndef SONGLIBRARY_H
#define SONGLIBRARY_H

#include <string>
#include <unordered_map>
#include <vector>

using MSong = std::unordered_map<std::string, std::string>;

// abstraction layer ovre DB
// no duplicate (file path)
class SongLibrary {
public:
  explicit SongLibrary();
  // add to library if not exists, else do nothing, return primary key
  int addTolibrary(MSong &&);
  const MSong &getSongByPK(int) const;
  std::vector<int> query(std::string) const;
  const std::vector<int> &queryView(std::string) const;
  std::vector<const std::string *> queryField(std::string);
  // TODO: remove, by marking as deleted
  // TODO: serialize, skipping songs marked deleted
  // TODO: deserialize

private:
  std::vector<MSong> songs;
  std::unordered_map<std::string, int> paths;
};

#endif // SONGLIBRARY_H
