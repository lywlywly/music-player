#include "lyricspanel.h"
#include "ui_lyricspanel.h"
#include <QAbstractTextDocumentLayout>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextBlock>
#include <algorithm>

LyricsPanel::LyricsPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::LyricsPanel) {
  ui->setupUi(this);
  QTextEdit *textEdit = ui->textEdit;
  // A list of lines, not text: a click seeks instead of selecting, so there
  // is nothing to copy and no context menu. Read-only, as the flags lack
  // TextEditable; setReadOnly(true) would bring mouse selection back.
  textEdit->setTextInteractionFlags(Qt::NoTextInteraction);
  textEdit->setFocusPolicy(Qt::NoFocus);
  textEdit->setContextMenuPolicy(Qt::NoContextMenu);
  textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
  textEdit->viewport()->setMouseTracking(true);
  textEdit->viewport()->setCursor(Qt::ArrowCursor);
  textEdit->viewport()->installEventFilter(this);

  // Scrolling moves lines under a pointer that does not move, which sends no
  // mouse move: find the hovered line again from the last one.
  connect(textEdit->verticalScrollBar(), &QScrollBar::valueChanged, this,
          [this]() {
            if (pointerPos_) {
              setHoveredLine(lineAt(*pointerPos_));
            }
          });

  holdTimer_.setSingleShot(true);
  holdTimer_.setInterval(kScrollHoldMs);
  connect(&holdTimer_, &QTimer::timeout, this, [this]() {
    followingHeld_ = false;
    const int target = centerScrollValue(currentLine_);
    if (target >= 0) {
      smoothScrollTo(target);
    }
  });
}

LyricsPanel::~LyricsPanel() { delete ui; }

void LyricsPanel::updateLyricsPanel(int index) { scrollToIndexCenter(index); }

void LyricsPanel::setLyricsPanel(const std::map<int, std::string> &map) {
  if (anim_) {
    anim_->stop();
  }
  releaseHold();
  lineCount_ = static_cast<int>(map.size());
  currentLine_ = -1;
  hoveredLine_ = -1;
  pressedLine_ = -1;
  ui->textEdit->setExtraSelections({});
  ui->textEdit->clear();
  ui->textEdit->setAlignment(Qt::AlignHCenter);
  for (const auto &[key, value] : map) {
    ui->textEdit->append(QString::fromUtf8(value));
  }
  // append() keeps the view pinned to the bottom; start at the first line.
  ui->textEdit->moveCursor(QTextCursor::Start);
  ui->textEdit->verticalScrollBar()->setValue(0);
  if (pointerPos_) {
    setHoveredLine(lineAt(*pointerPos_));
  }
}

void LyricsPanel::setLyricsTextFont(const QFont &font) {
  ui->textEdit->setFont(font);
}

void LyricsPanel::setHighlightTextColor(const QColor &color) {
  highlightTextColor_ = color;
  updateSelections();
}

void LyricsPanel::smoothScrollTo(int targetY, int durationMs) {
  if (anim_) {
    anim_->stop();
  }
  QScrollBar *vbar = ui->textEdit->verticalScrollBar();
  anim_ = new QPropertyAnimation(vbar, "value", this);
  anim_->setDuration(durationMs);
  anim_->setStartValue(vbar->value());
  anim_->setEndValue(targetY);
  anim_->setEasingCurve(QEasingCurve::OutCubic);
  anim_->start(QAbstractAnimation::DeleteWhenStopped);
}

int LyricsPanel::centerScrollValue(int index) const {
  QTextBlock block = ui->textEdit->document()->findBlockByNumber(index);
  if (!block.isValid()) {
    return -1;
  }
  QRectF rect =
      ui->textEdit->document()->documentLayout()->blockBoundingRect(block);
  int centerY = static_cast<int>(rect.center().y() -
                                 ui->textEdit->viewport()->height() / 2);
  QScrollBar *vbar = ui->textEdit->verticalScrollBar();
  return std::clamp(centerY, vbar->minimum(), vbar->maximum());
}

void LyricsPanel::scrollToIndexCenter(int index) {
  const int target = centerScrollValue(index);
  if (target < 0) {
    return;
  }
  currentLine_ = index;
  if (!followingHeld_) {
    smoothScrollTo(target);
  }
  updateSelections();
}

void LyricsPanel::recenterCurrentLine() {
  if (currentLine_ < 0 || followingHeld_) {
    return;
  }
  const int target = centerScrollValue(currentLine_);
  if (target < 0) {
    return;
  }
  if (anim_) {
    anim_->stop();
  }
  ui->textEdit->verticalScrollBar()->setValue(target);
}

void LyricsPanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  recenterCurrentLine();
}

void LyricsPanel::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  recenterCurrentLine();
}

bool LyricsPanel::eventFilter(QObject *watched, QEvent *event) {
  if (watched != ui->textEdit->viewport()) {
    return QWidget::eventFilter(watched, event);
  }
  switch (event->type()) {
  case QEvent::MouseMove: {
    pointerPos_ = static_cast<QMouseEvent *>(event)->position().toPoint();
    setHoveredLine(lineAt(*pointerPos_));
    // Keep holding while the user aims at a line.
    if (followingHeld_) {
      holdTimer_.start();
    }
    return true;
  }
  case QEvent::MouseButtonPress:
  case QEvent::MouseButtonDblClick: {
    // The second press of a double click arrives as a DblClick: count it as
    // a press, so a quick second click on another line seeks too.
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() == Qt::LeftButton) {
      pressedLine_ = lineAt(mouse->position().toPoint());
    }
    return true;
  }
  case QEvent::MouseButtonRelease: {
    // Seeks only on the line that was pressed: moving off it cancels, as
    // with a button.
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() == Qt::LeftButton) {
      const int line = lineAt(mouse->position().toPoint());
      const bool clicked = line >= 0 && line == pressedLine_;
      pressedLine_ = -1;
      if (clicked) {
        // The seek's line change scrolls to the clicked line; scrolling
        // back to the old line first would move the view away and back.
        releaseHold();
        emit lineClicked(line);
      }
    }
    return true;
  }
  case QEvent::Leave:
    pointerPos_.reset();
    setHoveredLine(-1);
    return false;
  case QEvent::Wheel:
    // The text edit scrolls (or zooms with Ctrl) as usual.
    holdFollowing();
    return false;
  default:
    return false;
  }
}

int LyricsPanel::lineAt(const QPoint &viewportPos) const {
  // cursorForPosition returns the nearest block, also below the last line;
  // the point must lie in the block's rectangle (document coordinates).
  const QTextBlock block = ui->textEdit->cursorForPosition(viewportPos).block();
  if (block.blockNumber() >= lineCount_) {
    return -1;
  }
  const QRectF rect =
      ui->textEdit->document()->documentLayout()->blockBoundingRect(block);
  const qreal y = viewportPos.y() + ui->textEdit->verticalScrollBar()->value();
  return (y >= rect.top() && y < rect.bottom()) ? block.blockNumber() : -1;
}

void LyricsPanel::setHoveredLine(int index) {
  if (index == hoveredLine_) {
    return;
  }
  hoveredLine_ = index;
  ui->textEdit->viewport()->setCursor(index >= 0 ? Qt::PointingHandCursor
                                                 : Qt::ArrowCursor);
  updateSelections();
}

void LyricsPanel::holdFollowing() {
  if (anim_) {
    anim_->stop();
  }
  followingHeld_ = true;
  holdTimer_.start();
}

void LyricsPanel::releaseHold() {
  followingHeld_ = false;
  holdTimer_.stop();
}

void LyricsPanel::updateSelections() {
  QTextCharFormat format;
  format.setForeground(highlightTextColor_);
  QList<QTextEdit::ExtraSelection> selections;
  for (const int line : {currentLine_, hoveredLine_}) {
    if (line >= 0) {
      selections.append(lineSelection(line, format));
    }
  }
  ui->textEdit->setExtraSelections(selections);
}

QTextEdit::ExtraSelection
LyricsPanel::lineSelection(int index, QTextCharFormat format) const {
  const QTextBlock block = ui->textEdit->document()->findBlockByNumber(index);
  QTextCursor cursor(block);
  cursor.setPosition(block.position() + block.length() - 1,
                     QTextCursor::KeepAnchor);
  // Spans the panel's width; a blank line, with no text to select, too.
  format.setProperty(QTextFormat::FullWidthSelection, true);
  return {cursor, format};
}
