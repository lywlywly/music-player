#include "lyricsmanager.h"

#include <QDebug>

LyricsManager::LyricsManager(QObject *parent) : QObject{parent} {}

void LyricsManager::setCurrentLyricsMap(QMap<int, QString> map) {
  lyricsMap = map;
}

const QMap<int, QString> &LyricsManager::getCurrentLyricsMap() const {
  return lyricsMap;
}

void LyricsManager::onPlayerProgressChange(qint64 progress) {
  QList<int> keys = lyricsMap.keys();
  auto it0 = std::find_if(keys.rbegin(), keys.rend(), [progress](int num) {
    return num <= static_cast<int>(progress);
  });

  int index = keys.size() - 1 - std::distance(keys.rbegin(), it0);

  if (lyricsMap.isEmpty()) return;

  if (index != currentLineIndex) {
    // qDebug() << "value" << *it0;
    // qDebug() << index;
    qDebug() << lyricsMap.values()[index];
    currentLineIndex = index;
    emit newLyricsLineIndex(currentLineIndex);
  }
}
