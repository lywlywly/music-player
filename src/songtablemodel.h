#ifndef SONGTABLEMODEL_H
#define SONGTABLEMODEL_H

#include "qurl.h"
#include <QStandardItemModel>
#include <limits>

enum class Field {
  ARTIST,
  TITLE,
  GENRE,
  BPM,
  REPLAY_GAIN,
  RATING,
  COMMENT,
  FILE_PATH
};
struct Song {
  QString artist;
  QString title;
  QString genre;
  int bpm=-1;
  double replaygain=std::numeric_limits<double>::quiet_NaN();
  int rating=-1;
  QString comment;
  QString filePath;
};
class SongTableModel {
  // TODO: inheritance instead of composition to save extra memory and overhead
  // to maintain data structures in two classes
public:
  SongTableModel(QObject *parent);
  SongTableModel(int, QList<Field> &, QObject * = nullptr);

  void setModelFields(QList<Field> &);
  QStandardItemModel *getModel();
  QUrl getEntryUrl(const QModelIndex &index);
  void customAppendRow(const QList<QStandardItem *> &items);
  void appendSong(const QUrl songPath);
  void setColumns(QList<Field>);
  void hideColumns(QList<Field>);

private:
  Song parseFile(QUrl);
  QList<QStandardItem*> songToItemList(const Song& song);
  QJsonValue getValue(const QJsonObject &jsonObject, Field field);
  QStandardItemModel *model;
  QList<Field> modelFieldList;
  QVector<Song> songs; // Data storage for songs
  std::unordered_map<Field, QString> enumToString = {{Field::ARTIST, "Artist"},
                                                     {Field::TITLE, "Title"},
                                                     {Field::GENRE, "Genre"},
                                                     {Field::BPM, "BPM"},
                                                     {Field::REPLAY_GAIN, "Replay Gain"},
                                                     {Field::RATING, "Rating"},
                                                     {Field::COMMENT, "Comment"},
                                                     {Field::FILE_PATH, "File Path"}};
};

#endif // SONGTABLEMODEL_H
