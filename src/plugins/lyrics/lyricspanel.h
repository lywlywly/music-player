#ifndef LYRICSPANEL_H
#define LYRICSPANEL_H

#include <QColor>
#include <QPointer>
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
  void setLyricsTextFont(const QFont &font);
  void setHighlightTextColor(const QColor &color);

protected:
  // Re-center the current line: the panel may have been resized while hidden
  // (a QTabWidget sizes only its current page), and the last scroll used the
  // viewport height of that time.
  void showEvent(QShowEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  void smoothScrollTo(int targetY, int durationMs = 300);
  void scrollToIndexCenter(int index);
  // The scroll value that centers block number index, or -1 for no such
  // block.
  int centerScrollValue(int index) const;
  // Scrolls to the current line without animation.
  void recenterCurrentLine();
  void colorLineText(const QTextBlock &block);
  Ui::LyricsPanel *ui;
  QPointer<QPropertyAnimation> anim_;
  QColor highlightTextColor_ = QColor(0, 100, 255);
  QTextEdit::ExtraSelection currentSelection_;
  // Block number of the highlighted line, or -1.
  int currentLine_ = -1;
};

#endif // LYRICSPANEL_H
