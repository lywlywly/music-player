#include "songlibrary.h"

SongLibrary::SongLibrary() { songs.reserve(10); }

int SongLibrary::addTolibrary(MSong &&s) {
  std::string p = s.at("path");

  if (paths.find(p) != paths.end()) {
    return paths[p];
  }

  songs.push_back(std::move(s));
  int pk = songs.size() - 1;
  paths[p] = pk;

  return pk;
}

std::vector<int> SongLibrary::query(std::string artist) const {
  std::vector<int> view;
  view.reserve(songs.size());
  for (int i = 0; i < songs.size(); i++) {
    const MSong &s = songs[i];
    if (s.at("artist") == artist) {
      view.push_back(i);
    }
  }
  return view;
}

const MSong &SongLibrary::getSongByPK(int pk) const { return songs[pk]; }
