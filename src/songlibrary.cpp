#include "songlibrary.h"
#include "databasemanager.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

SongLibrary::SongLibrary() { songs.reserve(10); }

int SongLibrary::addTolibrary(MSong &&s) {
  std::string p = s.at("path");
  int pk = songs.size();

  if (paths.find(p) != paths.end()) {
    return paths[p];
  }

  for (auto &[field, set] : registeredQueryFields) {
    if (set.find(s.at(field)) == set.end()) {
      set.insert(s.at(field));
    }
  }

  for (auto &[artist, vec] : registeredQueries) {
    if (s.at("artist") == artist)
      vec.push_back(pk);
  }

  songs.push_back(std::move(s));
  paths[p] = pk;

  return pk;
}

std::vector<int> SongLibrary::query(std::string artist) const {
  std::vector<int> view;
  for (int i = 0; i < songs.size(); i++) {
    const MSong &s = songs[i];
    if (s.at("artist") == artist) {
      view.push_back(i);
    }
  }

  return view;
}

const std::vector<int> &SongLibrary::registerQuery(std::string artist) const {
  std::vector<int> view;
  for (int i = 0; i < songs.size(); i++) {
    const MSong &s = songs[i];
    if (s.at("artist") == artist) {
      view.push_back(i);
    }
  }

  registeredQueries.emplace(artist, std::move(view));
  return registeredQueries.at(artist);
}

void SongLibrary::unregisterQuery(std::string artist) const {
  registeredQueries.erase(artist);
}

std::unordered_set<std::string>
SongLibrary::queryField(std::string field) const {
  std::unordered_set<std::string> set;
  for (const auto &song : songs) {
    set.insert(song.at(field));
  }

  return set;
}

const std::unordered_set<std::string> &
SongLibrary::registerQueryField(std::string field) const {
  std::unordered_set<std::string> set;
  for (const auto &song : songs) {
    set.insert(song.at(field));
  }

  registeredQueryFields.emplace(field, std::move(set));
  return registeredQueryFields.at(field);
}

void SongLibrary::unRegisterQueryField(std::string field) const {
  registeredQueryFields.erase(field);
}

bool SongLibrary::loadAll(QSqlDatabase &db) {
  // auto &db = DatabaseManager::db();

  int maxId = DatabaseManager::scalarInt(
      db, "SELECT IFNULL(MAX(song_id), 0) FROM songs");
  songs.clear();
  songs.resize(maxId + 1);
  paths.clear();
  paths.reserve(maxId + 1);

  QSqlQuery q(db);
  if (!q.exec(R"(
        SELECT song_id, title, artist, album, filepath
        FROM songs
        ORDER BY song_id
    )")) {
    qWarning() << "SongLibrary loadAll error:" << q.lastError().text();
    return false;
  }

  while (q.next()) {
    int id = q.value(0).toInt();
    if (id < 0)
      continue;
    if (id >= (int)songs.size())
      songs.resize(id + 1);

    auto &m = songs[id];
    m.reserve(5);
    // m.emplace("song_id", std::to_string(id));
    m.emplace("title", q.value(1).toString().toStdString());
    m.emplace("artist", q.value(2).toString().toStdString());
    m.emplace("album", q.value(3).toString().toStdString());
    std::string path = q.value(4).toString().toStdString();
    m.emplace("path", path);

    paths.emplace(std::move(path), id);
  }

  return true;
}

const MSong &SongLibrary::getSongByPK(int pk) const { return songs[pk]; }
