#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QList>
#include <QMouseEvent>
#include <QObject>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QTextBlock>
#include <QTextEdit>
#include <QWheelEvent>
#include <algorithm>
#include <map>
#include <string>

#include "../pluginapi/iplayercontrol.h"
#include "../pluginapi/iplayerhost.h"
#include "../plugins/lyrics/lyricspanel.h"
#include "../plugins/lyrics/lyricspresenter.h"

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

// The middle of line index, in viewport coordinates.
QPoint linePoint(QTextEdit *textEdit, int index) {
  const QTextBlock block = textEdit->document()->findBlockByNumber(index);
  const QRectF rect =
      textEdit->document()->documentLayout()->blockBoundingRect(block);
  return {textEdit->viewport()->width() / 2,
          static_cast<int>(rect.center().y()) -
              textEdit->verticalScrollBar()->value()};
}

void sendMouseMove(QWidget *viewport, const QPoint &pos) {
  QMouseEvent move(QEvent::MouseMove, pos, viewport->mapToGlobal(pos),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(viewport, &move);
}

void sendWheel(QWidget *viewport) {
  const QPoint pos(viewport->width() / 2, viewport->height() / 2);
  QWheelEvent wheel(pos, viewport->mapToGlobal(pos), QPoint(), QPoint(0, -120),
                    Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(viewport, &wheel);
}

// A host that keeps the one listener it is given.
class SingleListenerHost : public IPlayerHost {
public:
  void addListener(IPlayerListener *added) override { listener = added; }
  void removeListener(IPlayerListener *) override { listener = nullptr; }

  IPlayerListener *listener = nullptr;
};

// Records the commands it is given, as text.
class RecordingControl : public IPlayerControl {
public:
  void seek(qint64 positionMs) override {
    commands << "seek " + QString::number(positionMs);
  }
  void play() override { commands << "play"; }

  QStringList commands;
};
} // namespace

class TestLyricsPanel : public QObject {
  Q_OBJECT

private slots:
  void hiddenResize_recentersCurrentLineOnShow();
  void newLyrics_forgetCurrentLine();
  void click_onLineSendsLineClicked();
  void hover_highlightsLineAsCurrentLine();
  void wheel_holdsFollowingUntilTimeoutOrClick();
  void presenter_clickPlaysFromLineTimestamp();
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

void TestLyricsPanel::click_onLineSendsLineClicked() {
  LyricsPanel panel;
  panel.resize(240, 300);
  panel.setLyricsPanel(manyLines(3));
  panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));
  auto *textEdit = panel.findChild<QTextEdit *>();
  QWidget *viewport = textEdit->viewport();
  QSignalSpy clicked(&panel, &LyricsPanel::lineClicked);

  QTest::mouseClick(viewport, Qt::LeftButton, {}, linePoint(textEdit, 1));
  QCOMPARE(clicked.count(), 1);
  QCOMPARE(clicked.takeFirst().at(0).toInt(), 1);

  // Released on another line: cancelled.
  QTest::mousePress(viewport, Qt::LeftButton, {}, linePoint(textEdit, 0));
  QTest::mouseRelease(viewport, Qt::LeftButton, {}, linePoint(textEdit, 2));
  // Below the last line.
  const QPoint below(viewport->width() / 2, viewport->height() - 5);
  QTest::mouseClick(viewport, Qt::LeftButton, {}, below);
  // An empty panel still has one empty block.
  panel.setLyricsPanel({});
  QTest::mouseClick(viewport, Qt::LeftButton, {}, linePoint(textEdit, 0));
  QCOMPARE(clicked.count(), 0);
}

void TestLyricsPanel::hover_highlightsLineAsCurrentLine() {
  LyricsPanel panel;
  panel.resize(240, 300);
  panel.setLyricsPanel(manyLines(5));
  panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));
  auto *textEdit = panel.findChild<QTextEdit *>();
  QWidget *viewport = textEdit->viewport();
  panel.setHighlightTextColor(Qt::red);
  panel.updateLyricsPanel(1);

  sendMouseMove(viewport, linePoint(textEdit, 3));
  const QList<QTextEdit::ExtraSelection> selections =
      textEdit->extraSelections();
  QCOMPARE(selections.size(), 2);
  QCOMPARE(selections.at(0).cursor.block().blockNumber(), 1);
  QCOMPARE(selections.at(1).cursor.block().blockNumber(), 3);
  // The current line's highlight.
  QCOMPARE(selections.at(1).format.foreground().color(), QColor(Qt::red));
  QCOMPARE(viewport->cursor().shape(), Qt::PointingHandCursor);

  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(viewport, &leave);
  QCOMPARE(textEdit->extraSelections().size(), 1);
  QCOMPARE(viewport->cursor().shape(), Qt::ArrowCursor);
}

void TestLyricsPanel::wheel_holdsFollowingUntilTimeoutOrClick() {
  LyricsPanel panel;
  panel.resize(240, 200);
  panel.setLyricsPanel(manyLines(80));
  panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));
  auto *textEdit = panel.findChild<QTextEdit *>();
  QWidget *viewport = textEdit->viewport();
  QScrollBar *vbar = textEdit->verticalScrollBar();
  panel.updateLyricsPanel(40);
  QTRY_COMPARE(vbar->value(), centeredValue(textEdit, 40));

  sendWheel(viewport);
  const int scrolled = vbar->value();
  panel.updateLyricsPanel(41);
  QTest::qWait(400);
  QCOMPARE(vbar->value(), scrolled);
  // The hold ends kScrollHoldMs after the wheel; the view returns.
  QTRY_COMPARE_WITH_TIMEOUT(vbar->value(), centeredValue(textEdit, 41),
                            LyricsPanel::kScrollHoldMs + 2000);

  // A click on a line ends the hold at once.
  sendWheel(viewport);
  QTest::mouseClick(viewport, Qt::LeftButton, {}, linePoint(textEdit, 42));
  panel.updateLyricsPanel(42);
  QTRY_COMPARE_WITH_TIMEOUT(vbar->value(), centeredValue(textEdit, 42), 1000);
}

void TestLyricsPanel::presenter_clickPlaysFromLineTimestamp() {
  SingleListenerHost host;
  RecordingControl control;
  LyricsPanel panel;
  new LyricsPresenter(&panel, host, control);
  panel.resize(240, 300);
  panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));
  auto *textEdit = panel.findChild<QTextEdit *>();

  QVERIFY(host.listener != nullptr);
  host.listener->trackStarted(
      {.fields = {{"attr:lyrics", "[00:01.00]One\n[00:02.50]Two\n"}}});
  QTest::mouseClick(textEdit->viewport(), Qt::LeftButton, {},
                    linePoint(textEdit, 1));
  // The seek first, so a paused song resumes at the line.
  QCOMPARE(control.commands, (QStringList{"seek 2500", "play"}));
}

QTEST_MAIN(TestLyricsPanel)
#include "tst_lyricspanel.moc"
