#include "lyricshandler.h"

#include <QDebug>
#include <QTextStream>
#include <format>
#include <regex>

LyricsHandler::LyricsHandler(QObject *parent) : QObject{parent} {}

QMap<int, QString> LyricsHandler::getLyrics(QString title, QString artist) {
  QFile file = findFileByTitleAndArtist(title.toStdString(), artist.toStdString());
  QString filename = "/home/luyao/Music/茅原実里 - みちしるべ.lrc";
  QMap<int, QString> map;
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qDebug() << "Could not open file: " << file.errorString();
  }

  std::regex pattern(R"(\[(.*?)\](.*)$)");
  QTextStream in(&file);
  while (!in.atEnd()) {
    QString line = in.readLine();
    std::smatch matches;
    // TODO: why have to use s as a separate variable
    std::string s = line.toStdString();
    if (std::regex_match(s, matches, pattern)) {
      // Extract the first and second parts
      std::string timeString = matches[1];
      // Split the string into minutes, seconds, and milliseconds
      int minutes = std::stoi(timeString.substr(0, 2));
      int seconds = std::stoi(timeString.substr(3, 2));
      float milliseconds = std::stof(timeString.substr(6)) * 10;
      // Convert minutes and seconds to milliseconds
      int totalMilliseconds = (minutes * 60 + seconds) * 1000 + milliseconds;
      QString secondPart = QString::fromStdString(matches[2]);
      map.insert(totalMilliseconds, secondPart);
    }
    qDebug() << line;
  }

  file.close();

  return map;
}

QFile LyricsHandler::findFileByTitleAndArtist(std::string title,
                                              std::string artist) {
  std::string path =
      std::format("/home/luyao/Music/{} - {}.lrc", artist, title);
  // std::string path1 = "123Ab张顺飞c";
  // qDebug() << path1;
  qDebug() << QString::fromStdString(path);
  return QFile{QString::fromStdString(path)};
}
