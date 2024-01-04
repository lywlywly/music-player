#ifndef LYRICSHANDLER_H
#define LYRICSHANDLER_H

#include <QObject>
#include <QFile>

class LyricsHandler : public QObject
{
  Q_OBJECT
public:
  explicit LyricsHandler(QObject *parent = nullptr);
  QMap<int, QString> getLyrics(QString title, QString artist);
signals:

private:
  QFile findFileByTitleAndArtist(std::string title, std::string artist);
};

#endif // LYRICSHANDLER_H
