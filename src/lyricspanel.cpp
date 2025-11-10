#include "lyricspanel.h"
#include "ui_lyricspanel.h"
#include <QScrollBar>

LyricsPanel::LyricsPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::LyricsPanel) {
  ui->setupUi(this);
  ui->textEdit->setReadOnly(true);
}

LyricsPanel::~LyricsPanel() { delete ui; }

void getVisibleTextRange(QTextEdit *textEdit) {
  QTextCursor cursor = textEdit->textCursor();
  int cursorPosition = cursor.position();
  QScrollBar *verticalScrollBar = textEdit->verticalScrollBar();
  qDebug() << verticalScrollBar->value();
  qDebug() << cursor.blockNumber() << cursorPosition;
}

void LyricsPanel::updateLyricsPanel(int index) {
  // Get the text cursor of the QTextEdit
  QTextCursor cursor = ui->textEdit->textCursor();
  QTextBlockFormat format = cursor.blockFormat();

  cursor.movePosition(QTextCursor::Start);
  cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, index);
  format = cursor.blockFormat();
  format.setBackground(QColor(Qt::blue));
  cursor.setBlockFormat(format);

  cursor.movePosition(QTextCursor::Start);
  cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, index - 1);
  format = cursor.blockFormat();
  format.setBackground(QColor(Qt::transparent));
  cursor.setBlockFormat(format);
  cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, 1);

  ui->textEdit->setTextCursor(cursor);
  ui->textEdit->ensureCursorVisible();

  getVisibleTextRange(ui->textEdit);
}

void LyricsPanel::setLyricsPanel(const std::map<int, std::string> &map) {
  ui->textEdit->clear();
  for (const auto &[key, value] : map) {
    ui->textEdit->append(QString::fromUtf8(value));
  }
}
