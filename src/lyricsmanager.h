#ifndef LYRICSMANAGER_H
#define LYRICSMANAGER_H

#include <QMap>
#include <QObject>

class LyricsManager : public QObject {
  Q_OBJECT
 public:
  explicit LyricsManager(QObject *parent = nullptr);
  void setCurrentLyricsMap(QMap<int, QString>);
  const QMap<int, QString> &getCurrentLyricsMap() const;
 public slots:
  void onPlayerProgressChange(qint64 progress);
 signals:
  void newLyricsLineIndex(int newIndex);

 private:
  QMap<int, QString> lyricsMap;
  int currentLineIndex = -1;
};

#endif  // LYRICSMANAGER_H
