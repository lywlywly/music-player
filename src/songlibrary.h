#ifndef SONGLIBRARY_H
#define SONGLIBRARY_H

#include <QSqlDatabase>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
  const std::vector<int> &registerQuery(std::string) const;
  void unregisterQuery(std::string) const;
  // no duplicate
  // std::unordered_set<const std::string*, StringPtrHash, StringPtrEqual>
  // queryField(std::string) const;
  std::unordered_set<std::string> queryField(std::string) const;
  const std::unordered_set<std::string> &registerQueryField(std::string) const;
  void unRegisterQueryField(std::string) const;
  // TODO: remove, by marking as deleted, also update registered queries
  // TODO: serialize, skipping songs marked deleted
  // TODO: deserialize
  bool loadAll(QSqlDatabase &);

private:
  std::vector<MSong> songs;
  std::unordered_map<std::string, int> paths;
  mutable std::unordered_map<std::string, std::unordered_set<std::string>>
      registeredQueryFields;
  mutable std::unordered_map<std::string, std::vector<int>> registeredQueries;
};

#endif // SONGLIBRARY_H
