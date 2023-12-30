#include "songparser.h"

#include <regex>

#include "qjsondocument.h"
#include "qprocess.h"

extern "C" {
#include "libavformat/avformat.h"
}

SongParser::SongParser() {}

QJsonValue SongParser::getValue(const QJsonObject& jsonObject, Field field) {
  QJsonValue value;
  for (const QString key : fieldMap[field]) {
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

QVariant SongParser::getValue(const QMap<QString, QString>& jsonObject,
                              Field field) {
  QVariant value;

  for (const QString key : fieldMap[field]) {
    value = jsonObject[key];
    if (!value.toString().isEmpty()) {
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
      value = QString::fromStdString(extractedNumber).toDouble();
    } else {
      value = 0;
    }
  }

  if (field == Field::BPM || field == Field::RATING) {
    value = value.toInt();
  }

  return value;
}

Song SongParser::parseFileLegacy(QUrl songPath) {
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

Song SongParser::parseFile(QUrl songPath) {
  avformat_network_init();
  AVFormatContext* formatContext = nullptr;
  if (avformat_open_input(&formatContext, songPath.toString().toUtf8().data(),
                          nullptr, nullptr) != 0) {
    qDebug() << "Failed to open file";
  }

  if (avformat_find_stream_info(formatContext, nullptr) < 0) {
    qDebug() << "Failed to find stream information";
    avformat_close_input(&formatContext);
  }

  AVDictionaryEntry* metadata = nullptr;
  QMap<QString, QString> map;
  while ((metadata = av_dict_get(formatContext->metadata, "", metadata,
                                 AV_DICT_IGNORE_SUFFIX))) {
    map.insert(metadata->key, metadata->value);
  }

  avformat_close_input(&formatContext);

  Song song;
  song.artist = getValue(map, Field::ARTIST).toString();
  song.title = getValue(map, Field::TITLE).toString();
  song.genre = getValue(map, Field::GENRE).toString();
  song.bpm = getValue(map, Field::BPM).toInt();
  song.replaygain = getValue(map, Field::REPLAY_GAIN).toDouble();
  song.rating = getValue(map, Field::RATING).toInt();
  song.filePath = songPath.toString();
  return song;
}
