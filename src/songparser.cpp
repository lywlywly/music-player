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

QVariant SongParser::getValue(const QMap<QString, QString>& metaDataMap,
                              Field field) {
  QVariant value;

  for (const QString key : fieldMap[field]) {
    value = metaDataMap[key];
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

std::tuple<std::unique_ptr<uchar[]>, int> SongParser::parseSongCover(QUrl url) {
  avformat_network_init();
  std::string stdstring = url.toString().toStdString();
  const char* filename = stdstring.c_str();
  AVFormatContext* formatContext = NULL;
  AVPacket packet;
  int ret, streamIndex;
  // TODO: very large upper bound in development, reduce it in release
  int numMBs = 20;
  unsigned int numBytes = numMBs * 1024 * 1024;
  std::unique_ptr<uchar[]> data = std::make_unique<uchar[]>(numBytes);
  int size = -1;
  qDebug() << filename;
  // Open the input file
  ret = avformat_open_input(&formatContext, filename, NULL, NULL);
  if (ret < 0) {
    qDebug() << "Failed to open input file";
  }
  // Find the first stream in the input file
  ret = avformat_find_stream_info(formatContext, NULL);
  if (ret < 0) {
    qDebug() << "Failed to find stream info";
    avformat_close_input(&formatContext);
  }
  // Find the first cover art packet
  streamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1,
                                    nullptr, 0);
  if (streamIndex < 0) {
    qDebug() << "No cover art found";
    avformat_close_input(&formatContext);
    return std::make_tuple(std::move(data), size);
  }
  // Read packets until a cover art packet is found
  while (av_read_frame(formatContext, &packet) >= 0) {
    if (packet.stream_index == streamIndex) {
      // Save the cover art packet to a file
      FILE* outputFile = fopen("cover1.jpg", "wb");
      if (outputFile == NULL) {
        fprintf(stderr, "Failed to open output file\n");
        av_packet_unref(&packet);
        avformat_close_input(&formatContext);
      }
      if (packet.size > numBytes) {
        qDebug() << "cover larger then " << numMBs << "MB, aborted";
        return std::make_tuple(std::move(data), size);
      }
      fwrite(packet.data, 1, packet.size, outputFile);
      std::copy(packet.data, packet.data + packet.size, data.get());
      size = packet.size;
      qDebug() << "packet.size: " << size;
      if (outputFile != nullptr) {
        fclose(outputFile);
      }
      break;
    }
  }
  // Clean up
  av_packet_unref(&packet);
  avformat_close_input(&formatContext);

  return std::make_tuple(std::move(data), size);
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
    qDebug() << metadata->key << ": " << metadata->value;
  }

  if (0 == (std::strcmp(formatContext->iformat->name, "ogg"))) {
    qDebug() << "ogg";
    AVStream* st = formatContext->streams[0];
    AVDictionaryEntry* entry = NULL;
    while ((entry = av_dict_get(st->metadata, "", entry,
                                AV_DICT_IGNORE_SUFFIX)) != NULL) {
      qDebug() << entry->key << ": " << entry->value;
      map.insert(entry->key, entry->value);
    }
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
