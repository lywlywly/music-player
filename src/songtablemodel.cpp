#include "songtablemodel.h"
#include "qjsondocument.h"
#include "qjsonobject.h"
#include "qprocess.h"
#include "qurl.h"
#include <QStandardItemModel>
#include <algorithm>
#include <regex>

SongTableModel::SongTableModel(QObject *parent) {
  model = new QStandardItemModel(0, modelFieldList.size(), parent);
}

SongTableModel::SongTableModel(int initialSize, QList<Field> &modelFieldList,
                               QObject *parent) {
  songs.resize(initialSize);
  this->modelFieldList = modelFieldList;
  model = new QStandardItemModel(initialSize, modelFieldList.size(), parent);
  QList<QString> stringFields;
  std::transform(modelFieldList.begin(), modelFieldList.end(),
                 std::back_inserter(stringFields),
                 [this](Field field) { return enumToString[field]; });
  model->setHorizontalHeaderLabels(stringFields);
}

QStandardItemModel *SongTableModel::getModel() { return model; }

void SongTableModel::setColumns(QList<Field> modelFieldList) {
  this->modelFieldList = modelFieldList;
  QList<QString> stringFields;
  std::transform(modelFieldList.begin(), modelFieldList.end(),
                 std::back_inserter(stringFields),
                 [this](Field field) { return enumToString[field]; });
  model->setHorizontalHeaderLabels(stringFields);
}

QList<QStandardItem *> SongTableModel::songToItemList(const Song &song) {
  QList<QStandardItem *> itemList;
  itemList << new QStandardItem(song.artist) << new QStandardItem(song.title)
           << new QStandardItem(song.genre)
           << new QStandardItem(song.bpm < 0 ? "" : QString::number(song.bpm))
           << new QStandardItem(std::isnan(song.replaygain)
                                    ? ""
                                    : QString::number(song.replaygain))
           << new QStandardItem(song.rating < 0 ? ""
                                                : QString::number(song.rating))
           << new QStandardItem(song.comment)
           << new QStandardItem(song.filePath);
  return itemList;
}

void SongTableModel::setModelFields(QList<Field> &l) {
  if (model != nullptr) {
    delete model; // Free previous memory
  }
  model = new QStandardItemModel(0, l.size());
}

QUrl SongTableModel::getEntryUrl(const QModelIndex &index) {
  return songs[index.row()].filePath;
}

QJsonValue SongTableModel::getValue(const QJsonObject &jsonObject,
                                    Field field) {
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

Song SongTableModel::parseFile(QUrl songPath) {
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
  qDebug() << format;
  song.artist = getValue(format, Field::ARTIST).toString();
  song.title = getValue(format, Field::TITLE).toString();
  song.genre = getValue(format, Field::GENRE).toString();
  song.bpm = getValue(format, Field::BPM).toInt();
  song.replaygain = getValue(format, Field::REPLAY_GAIN).toDouble();
  song.rating = getValue(format, Field::RATING).toInt();
  song.filePath = songPath.toString();
  return song;
}

void SongTableModel::appendSong(const QUrl songPath) {
  Song song = parseFile(songPath);

  songs.append(song);
  model->appendRow(SongTableModel::songToItemList(song));
};
