#include "lyricsloader.h"

#include <QDebug>
#include <QTextStream>
#include <format>
#include <regex>

LyricsLoader::LyricsLoader(QObject *parent) : QObject{parent} {}

void testTmp(const std::string &) {}
void testTmp(const std::string &&) = delete;

QMap<int, QString> LyricsLoader::getLyrics(QString title, QString artist) {
  QFile file =
      findFileByTitleAndArtist(title.toStdString(), artist.toStdString());
  QString filename = "/home/luyao/Music/茅原実里 - みちしるべ.lrc";
  QMap<int, QString> map;
  QMap<QString, QString> metadata;
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qDebug() << "Could not open file: " << file.errorString();
  }

  std::regex patternMetadata{R"(\[([a-zA-Z]+):(.*)])"};
  std::regex patternTime{R"(\[(.*?)\])"};
  std::regex patternText{R"(\]([^\[\]]*)$)"};

  QTextStream in(&file);
  while (!in.atEnd()) {
    QString line = in.readLine();
    std::string s = line.toStdString();

    std::smatch matcheMetadata;
    if (std::regex_search(s, matcheMetadata, patternMetadata)) {
      std::string key = matcheMetadata.str(1);
      std::string value = matcheMetadata.str(2);
      metadata.insert(QString::fromStdString(key),
                      QString::fromStdString(value));
      continue;
    }

    std::smatch matches;
    QList<int> values;
    auto words_begin = std::sregex_iterator(s.begin(), s.end(), patternTime);
    auto words_end = std::sregex_iterator();
    for (std::sregex_iterator it = words_begin; it != words_end; ++it) {
      std::smatch match = *it;
      std::string timeString = match.str(1);
      int minutes = std::stoi(timeString.substr(0, 2));
      int seconds = std::stoi(timeString.substr(3, 2));
      float milliseconds = std::stof(timeString.substr(6)) * 10;
      int totalMilliseconds = (minutes * 60 + seconds) * 1000 + milliseconds;
      values.append(totalMilliseconds);
    }

    std::smatch textMatch;
    if (std::regex_search(s, textMatch, patternText)) {
      std::string last_text = textMatch.str(1);
      for (int ms : values) {
        map.insert(ms, QString::fromStdString(last_text));
      }
    }
  }
  file.close();

  qDebug() << metadata;

  return map;
}

QFile LyricsLoader::findFileByTitleAndArtist(std::string title,
                                             std::string artist) {
  std::string path =
      std::format("/home/luyao/Music/{} - {}.lrc", artist, title);
  // std::string path1 = "123Ab张顺飞c";
  // qDebug() << path1;
  qDebug() << QString::fromStdString(path);
  return QFile{QString::fromStdString(path)};
}
