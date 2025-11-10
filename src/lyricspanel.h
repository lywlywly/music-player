#ifndef LYRICSPANEL_H
#define LYRICSPANEL_H

#include <QWidget>

namespace Ui {
class LyricsPanel;
}

class LyricsPanel : public QWidget {
  Q_OBJECT

public:
  explicit LyricsPanel(QWidget *parent = nullptr);
  ~LyricsPanel();
  void updateLyricsPanel(int index);
  void setLyricsPanel(const std::map<int, std::string> &map);

private:
  Ui::LyricsPanel *ui;
};

#endif // LYRICSPANEL_H
