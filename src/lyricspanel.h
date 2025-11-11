#ifndef LYRICSPANEL_H
#define LYRICSPANEL_H

#include <QPropertyAnimation>
#include <QTextEdit>
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
  void smoothScrollTo(int targetY, int durationMs = 300);
  void scrollToIndexCenter(int index);
  void colorLineText(const QTextBlock &block);
  Ui::LyricsPanel *ui;
  QPropertyAnimation *anim_ = nullptr;
  QTextEdit::ExtraSelection currentSelection_;
};

#endif // LYRICSPANEL_H
