#include "songparser.h"

#include <regex>

#include "qjsondocument.h"
#include "qprocess.h"

SongParser::SongParser() {}

QJsonValue SongParser::getValue(const QJsonObject& jsonObject, Field field) {
  QMap<Field, QList<QString>> map = {
      {Field::ARTIST, {"artist", "ARTIST"}},
      {Field::TITLE, {"title", "TITLE"}},
      {Field::GENRE, {"genre"}},
      {Field::BPM, {"TBPM"}},
      {Field::REPLAY_GAIN, {"replaygain_track_gain"}},
      {Field::RATING, {"Rating"}},
  };
  QJsonValue value;
  for (const QString key : map[field]) {
    value = jsonObject[key];
    if (!value.toString().isNull()) {
      break;
    }
  }
  // post-processing
  if (field == Field::REPLAY_GAIN) {
    std::regex pattern(R"((-?\d+(?:\.\d+)?))");
    std::smatch match;
    std::string input = value.toString().toStdString();
    if (std::regex_search(input, match, pattern)) {
      std::string extractedNumber = match[1].str();
      value = QJsonValue(QString::fromStdString(extractedNumber).toDouble());
    } else {
      value = QJsonValue();
    }
  }
  if (field == Field::BPM || field == Field::RATING) {
    value = QJsonValue(value.toString().toInt());
  }
  return value;
}

Song SongParser::parseFile(QUrl songPath) {
  QProcess ffprobeProcess;
  ffprobeProcess.start("ffprobe", QStringList()
                                      << "-v"
                                      << "quiet"
                                      << "-print_format"
                                      << "json"
                                      << "-show_format" << songPath.toString());
  ffprobeProcess.waitForFinished();
  QByteArray outputData = ffprobeProcess.readAllStandardOutput();
  QString outputString = QString::fromUtf8(outputData);
  QJsonDocument jsonDoc = QJsonDocument::fromJson(outputString.toUtf8());
  QJsonObject jsonObj = jsonDoc.object();
  QJsonObject format = jsonObj["format"].toObject()["tags"].toObject();
  Song song;
  // qDebug() << format;
  song.artist = getValue(format, Field::ARTIST).toString();
  song.title = getValue(format, Field::TITLE).toString();
  song.genre = getValue(format, Field::GENRE).toString();
  song.bpm = getValue(format, Field::BPM).toInt();
  song.replaygain = getValue(format, Field::REPLAY_GAIN).toDouble();
  song.rating = getValue(format, Field::RATING).toInt();
  song.filePath = songPath.toString();
  return song;
}
