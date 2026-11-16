#ifndef LYRICSPANEL_H
#define LYRICSPANEL_H

#include <QColor>
#include <QPoint>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTextCharFormat>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>
#include <optional>

namespace Ui {
class LyricsPanel;
}

// The lyrics as a list of lines: the current line is highlighted and
// followed, the line under the pointer is highlighted the same way, and a
// click on a line sends lineClicked. Scrolling with the wheel holds following
// for kScrollHoldMs (docs/plugins/lyrics.md, "Seeking from a line, hover and
// the scroll hold").
class LyricsPanel : public QWidget {
  Q_OBJECT

public:
  explicit LyricsPanel(QWidget *parent = nullptr);
  ~LyricsPanel();
  void updateLyricsPanel(int index);
  void setLyricsPanel(const std::map<int, std::string> &map);
  void setLyricsTextFont(const QFont &font);
  void setHighlightTextColor(const QColor &color);

  // How long following stays held after the last wheel event, or the last
  // mouse move while held.
  static constexpr int kScrollHoldMs = 3000;

signals:
  // A click on line index (its block number, the index in the lyrics map).
  void lineClicked(int index);

protected:
  // Re-center the current line: the panel may have been resized while hidden
  // (a QTabWidget sizes only its current page), and the last scroll used the
  // viewport height of that time. Not while following is held.
  void showEvent(QShowEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  // The text edit's viewport: hover, click and the scroll hold.
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void smoothScrollTo(int targetY, int durationMs = 300);
  void scrollToIndexCenter(int index);
  // The scroll value that centers block number index, or -1 for no such
  // block.
  int centerScrollValue(int index) const;
  // Scrolls to the current line without animation.
  void recenterCurrentLine();
  // The line whose block spans the viewport point's y, or -1.
  int lineAt(const QPoint &viewportPos) const;
  void setHoveredLine(int index);
  void holdFollowing();
  void releaseHold();
  // Sets the extra selections: the current and the hovered line, both in the
  // highlight color.
  void updateSelections();
  QTextEdit::ExtraSelection lineSelection(int index,
                                          QTextCharFormat format) const;
  Ui::LyricsPanel *ui;
  QPointer<QPropertyAnimation> anim_;
  QColor highlightTextColor_ = QColor(0, 100, 255);
  // The number of lyrics lines: an empty panel still has one empty block.
  int lineCount_ = 0;
  // Block number of the highlighted line, or -1.
  int currentLine_ = -1;
  // Block number of the line under the pointer, or -1.
  int hoveredLine_ = -1;
  // The line a press landed on, until the release, or -1.
  int pressedLine_ = -1;
  // The pointer's last position in the viewport, while it is over it.
  std::optional<QPoint> pointerPos_;
  // While held, line changes recolor but do not scroll.
  bool followingHeld_ = false;
  QTimer holdTimer_;
};

#endif // LYRICSPANEL_H
