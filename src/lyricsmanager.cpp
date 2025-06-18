#include "lyricsmanager.h"

#include <QDebug>

LyricsManager::LyricsManager(QObject *parent) : QObject{parent} {}

void LyricsManager::setLyrics(std::map<int, std::string> &&map) {
  lyricsMap = std::move(map);
  QMap<int, QString> qmap;
  std::vector<int> _keys;
  for (const auto &[key, value] : lyricsMap) {
    qmap.insert(key, QString::fromUtf8(value));
    _keys.push_back(key);
  }
  keys = std::move(_keys);
}

const std::map<int, std::string> &LyricsManager::getCurrentLyricsMap() const {
  return lyricsMap;
}

void LyricsManager::onPlayerProgressChange(qint64 progress) {
  auto it0 = std::find_if(keys.rbegin(), keys.rend(), [progress](int num) {
    return num <= static_cast<int>(progress);
  });

  int index = keys.size() - 1 - std::distance(keys.rbegin(), it0);

  if (lyricsMap.empty())
    return;

  if (index != currentLineIndex) {
    // qDebug() << "value" << *it0;
    // qDebug() << index;
    // qDebug() << lyricsMap.values()[index];
    currentLineIndex = index;
    emit newLyricsLineIndex(currentLineIndex);
  }
}
