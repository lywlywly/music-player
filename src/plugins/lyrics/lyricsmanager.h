#ifndef LYRICSMANAGER_H
#define LYRICSMANAGER_H

#include <QMap>
#include <QObject>

class LyricsManager : public QObject {
  Q_OBJECT
public:
  explicit LyricsManager(QObject *parent = nullptr);
  void setLyrics(std::map<int, std::string> &&);
  const std::map<int, std::string> &getCurrentLyricsMap() const;
  void onPlayerProgressChange(qint64 progress);
signals:
  void newLyricsLineIndex(int newIndex);

private:
  std::map<int, std::string> lyricsMap;
  std::vector<int> keys;
  int currentLineIndex = -1;
};

#endif // LYRICSMANAGER_H
