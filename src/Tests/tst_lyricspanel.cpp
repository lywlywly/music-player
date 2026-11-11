#include <QAbstractTextDocumentLayout>
#include <QObject>
#include <QScrollBar>
#include <QTest>
#include <QTextBlock>
#include <QTextEdit>
#include <algorithm>
#include <map>
#include <string>

#include "../lyricspanel.h"

namespace {
std::map<int, std::string> manyLines(int count) {
  std::map<int, std::string> lines;
  for (int i = 0; i < count; ++i) {
    lines[i * 1000] = "Line " + std::to_string(i);
  }
  return lines;
}

// The scroll value that centers block number index in textEdit now.
int centeredValue(QTextEdit *textEdit, int index) {
  const QTextBlock block = textEdit->document()->findBlockByNumber(index);
  const QRectF rect =
      textEdit->document()->documentLayout()->blockBoundingRect(block);
  QScrollBar *vbar = textEdit->verticalScrollBar();
  return std::clamp(
      static_cast<int>(rect.center().y() - textEdit->viewport()->height() / 2),
      vbar->minimum(), vbar->maximum());
}
} // namespace

class TestLyricsPanel : public QObject {
  Q_OBJECT

private slots:
  void hiddenResize_recentersCurrentLineOnShow();
  void newLyrics_forgetCurrentLine();
};

void TestLyricsPanel::hiddenResize_recentersCurrentLineOnShow() {
  LyricsPanel panel;
  panel.resize(240, 200);
  panel.setLyricsPanel(manyLines(80));
  panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));
  auto *textEdit = panel.findChild<QTextEdit *>();
  QVERIFY(textEdit != nullptr);

  panel.updateLyricsPanel(40);
  QTRY_COMPARE(textEdit->verticalScrollBar()->value(),
               centeredValue(textEdit, 40));

  // Like a page of a QTabWidget: a line change while hidden, then a resize
  // that only takes effect when shown again.
  panel.hide();
  panel.updateLyricsPanel(41);
  panel.resize(240, 500);
  panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));
  QCOMPARE(textEdit->verticalScrollBar()->value(), centeredValue(textEdit, 41));
  // Stays there: the animation started while hidden was stopped.
  QTest::qWait(400);
  QCOMPARE(textEdit->verticalScrollBar()->value(), centeredValue(textEdit, 41));
}

void TestLyricsPanel::newLyrics_forgetCurrentLine() {
  LyricsPanel panel;
  panel.resize(240, 200);
  panel.setLyricsPanel(manyLines(80));
  panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));
  auto *textEdit = panel.findChild<QTextEdit *>();
  panel.updateLyricsPanel(40);

  panel.setLyricsPanel(manyLines(80));
  panel.resize(240, 300);
  QTest::qWait(50);
  QCOMPARE(textEdit->verticalScrollBar()->value(), 0);
}

QTEST_MAIN(TestLyricsPanel)
#include "tst_lyricspanel.moc"
