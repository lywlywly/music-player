#include "lyricspanel.h"
#include "ui_lyricspanel.h"
#include <QAbstractTextDocumentLayout>
#include <QScrollBar>
#include <QTextBlock>

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
  ui->textEdit->clear();
  for (const auto &[key, value] : map) {
    ui->textEdit->append(QString::fromUtf8(value));
  }
}

void LyricsPanel::smoothScrollTo(int targetY, int durationMs) {
  QScrollBar *vbar = ui->textEdit->verticalScrollBar();
  anim_ = new QPropertyAnimation(vbar, "value", this);
  anim_->setDuration(durationMs);
  anim_->setStartValue(vbar->value());
  anim_->setEndValue(targetY);
  anim_->setEasingCurve(QEasingCurve::OutCubic);
  anim_->start(QAbstractAnimation::DeleteWhenStopped);
}

void LyricsPanel::scrollToIndexCenter(int index) {
  QTextBlock block = ui->textEdit->document()->findBlockByNumber(index);
  if (!block.isValid())
    return;

  QRectF rect =
      ui->textEdit->document()->documentLayout()->blockBoundingRect(block);
  int centerY =
      static_cast<int>(rect.top() - ui->textEdit->viewport()->height() / 2);
  QScrollBar *vbar = ui->textEdit->verticalScrollBar();
  int target = std::clamp(centerY, vbar->minimum(), vbar->maximum());
  smoothScrollTo(target);
  colorLineText(block);
}

void LyricsPanel::colorLineText(const QTextBlock &block) {
  QTextCursor cursor(block);
  cursor.select(QTextCursor::LineUnderCursor);

  QTextEdit::ExtraSelection sel;
  sel.cursor = cursor;
  sel.format.setForeground(QColor(0, 100, 255));
  sel.format.setProperty(QTextFormat::FullWidthSelection, true);

  currentSelection_ = sel;
  ui->textEdit->setExtraSelections({currentSelection_});
}
