#include "lyricspanel.h"
#include "ui_lyricspanel.h"
#include <QAbstractTextDocumentLayout>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextBlock>
#include <algorithm>

LyricsPanel::LyricsPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::LyricsPanel) {
  ui->setupUi(this);
  ui->textEdit->setReadOnly(true);
  ui->textEdit->setVerticalScrollBarPolicy(
      Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
}

LyricsPanel::~LyricsPanel() { delete ui; }

void LyricsPanel::updateLyricsPanel(int index) { scrollToIndexCenter(index); }

void LyricsPanel::setLyricsPanel(const std::map<int, std::string> &map) {
  if (anim_) {
    anim_->stop();
  }
  currentSelection_ = {};
  currentLine_ = -1;
  ui->textEdit->setExtraSelections({});
  ui->textEdit->clear();
  ui->textEdit->setAlignment(Qt::AlignHCenter);
  for (const auto &[key, value] : map) {
    ui->textEdit->append(QString::fromUtf8(value));
  }
  // append() keeps the view pinned to the bottom; start at the first line.
  ui->textEdit->moveCursor(QTextCursor::Start);
  ui->textEdit->verticalScrollBar()->setValue(0);
}

void LyricsPanel::setLyricsTextFont(const QFont &font) {
  ui->textEdit->setFont(font);
}

void LyricsPanel::setHighlightTextColor(const QColor &color) {
  highlightTextColor_ = color;
  if (!currentSelection_.cursor.isNull()) {
    currentSelection_.format.setForeground(highlightTextColor_);
    ui->textEdit->setExtraSelections({currentSelection_});
  }
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
  smoothScrollTo(target);
  colorLineText(ui->textEdit->document()->findBlockByNumber(index));
}

void LyricsPanel::recenterCurrentLine() {
  if (currentLine_ < 0) {
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

void LyricsPanel::colorLineText(const QTextBlock &block) {
  QTextCursor cursor(block);
  cursor.setPosition(block.position());
  cursor.setPosition(block.position() + block.length() - 1,
                     QTextCursor::KeepAnchor);

  QTextEdit::ExtraSelection sel;
  sel.cursor = cursor;
  sel.format.setForeground(highlightTextColor_);
  sel.format.setProperty(QTextFormat::FullWidthSelection, true);

  currentSelection_ = sel;
  ui->textEdit->setExtraSelections({currentSelection_});
}
