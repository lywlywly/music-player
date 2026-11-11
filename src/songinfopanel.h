#ifndef SONGINFOPANEL_H
#define SONGINFOPANEL_H

#include "songinfo.h"
#include <QWidget>

class QBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;

// The Properties tab: read-only, copyable rows of one SongInfo, grouped by
// section, with Open Containing Folder and Edit Properties... buttons.
// Frontend class; SongInfoPresenter decides what it shows, and MainWindow
// handles its buttons.
class SongInfoPanel : public QWidget {
  Q_OBJECT

public:
  explicit SongInfoPanel(QWidget *parent = nullptr);

  // Shows info; an empty info shows "No song selected".
  void setInfo(const SongInfo &info);
  const SongInfo &info() const;

signals:
  void revealRequested(int songPk);
  void editPropertiesRequested(int songPk);

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  SongInfo info_;
  QScrollArea *scrollArea_ = nullptr;
  QLabel *placeholder_ = nullptr;
  QBoxLayout *buttons_ = nullptr;
  QPushButton *revealButton_ = nullptr;
  QPushButton *editButton_ = nullptr;
};

#endif // SONGINFOPANEL_H
