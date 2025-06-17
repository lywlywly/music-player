#ifndef LYRICSLOADER_H
#define LYRICSLOADER_H

#include <QFile>
#include <QObject>

// TODO: do not inherit QObject, this is a whole utility class outside Qt
// framework LyricsManager is responsible for providing the right line of lyrics
// instead
class LyricsLoader : public QObject {
  Q_OBJECT
public:
  explicit LyricsLoader(QObject *parent = nullptr);
  QMap<int, QString> getLyrics(QString title, QString artist);
signals:

private:
  QFile findFileByTitleAndArtist(std::string title, std::string artist);
};

#endif // LYRICSLOADER_H
