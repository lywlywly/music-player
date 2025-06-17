#include "songstore.h"
#include <unicode/coll.h>
#include <unicode/locid.h>

UErrorCode status = U_ZERO_ERROR;
icu::Locale loc = icu::Locale::forLanguageTag("zh-u-kr-latn-hani-hrkt", status);
// ordering based on simplified chinese pinyin, default ordering for other
// characters. Keep latin letters first, then Han (Hanzi, Kanji, Hanja), then
// Hiragana and Katakana
std::unique_ptr<icu::Collator> collator(icu::Collator::createInstance(loc,
                                                                      status));

SongStore::SongStore(SongLibrary &lib) : library{lib} {}

int SongStore::songCount() const { return songPKs.size(); }

void SongStore::addSong(MSong &&s)
{
  int s1 = library.addTolibrary(std::move(s));
  songPKs.push_back(s1);
  if (indices.size() < s1 + 1)
    indices.resize(s1 + 1, -1);
  indices[s1] = songPKs.size() - 1;
}

void SongStore::removeSongByPk(int pk)
{
  int i = indices[pk];
  songPKs.erase(songPKs.begin() + i); // TODO: lazy delete
  indices[pk] = -1;
}

void SongStore::removeSongByIndex(int i)
{
  int pk = songPKs[i];
  songPKs.erase(songPKs.begin() + i); // TODO: lazy delete
  indices[pk] = -1;
}

void SongStore::clear()
{
  songPKs.clear();
  indices.clear();
}

const MSong &SongStore::getSongByPk(int pk) const
{
  if (indices[pk] == -1)
  {
    throw std::logic_error("Key not in view");
  }
  return library.getSongByPK(pk);
}

const MSong &SongStore::getSongByIndex(int i) const
{
  return library.getSongByPK(songPKs.at(i));
}

const int SongStore::getPkByIndex(int i) const { return songPKs.at(i); }

const int SongStore::getIndexByPk(int pk) const
{
  if (indices[pk] == -1)
  {
    throw std::logic_error("Key not in view");
  }
  return indices.at(pk);
}

void SongStore::sortByField(std::string f, int order)
{
  UCollationResult result;
  if (order == 0)
    result = UCollationResult::UCOL_LESS;
  else
    result = UCollationResult::UCOL_GREATER;
  std::stable_sort(songPKs.begin(), songPKs.end(), [&](int i, int j)
                   {
    icu::UnicodeString ua =
        icu::UnicodeString::fromUTF8(library.getSongByPK(i).at(f));
    icu::UnicodeString ub =
        icu::UnicodeString::fromUTF8(library.getSongByPK(j).at(f));
    return collator->compare(ua, ub) == result; });

  std::fill(indices.begin(), indices.end(), -1);

  for (size_t i = 0; i < songPKs.size(); ++i)
  {
    indices[songPKs[i]] = i;
  }
}

const std::vector<int> &SongStore::getSongsView() const { return songPKs; }

const std::vector<int> &SongStore::getIndices() const { return indices; }
