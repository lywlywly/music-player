#include <QObject>
#include <QSettings>
#include <QTest>

#include "../columnregistry.h"
#include "../displayexpression.h"
#include "../statusruntimesymboltable.h"

class TestDisplayExpression : public QObject {
  Q_OBJECT

private slots:
  void init();
  void defaultExpressions_parse();
  void settings_blankMeansDefaultAndValuesAreTrimmed();
  void evaluate_runtimeFieldsWinOverSongFields();
  void evaluate_withoutSong_songFieldsAreEmpty();

private:
  QString evaluate(const QString &expression,
                   const StatusRuntimeSymbolTable &runtime, const MSong *song);

  ColumnRegistry registry_;
};

void TestDisplayExpression::init() { QSettings().clear(); }

QString TestDisplayExpression::evaluate(const QString &expression,
                                        const StatusRuntimeSymbolTable &runtime,
                                        const MSong *song) {
  const ExprParseResult parsed = parseDisplayExpression(expression, registry_);
  if (!parsed.ok()) {
    qWarning() << "parse failed:" << parsed.error.message;
    return QStringLiteral("<parse error>");
  }
  return evaluateDisplayExpression(*parsed.expr, runtime, song);
}

void TestDisplayExpression::defaultExpressions_parse() {
  for (DisplayExpressionKind kind :
       {DisplayExpressionKind::StatusBar, DisplayExpressionKind::WindowTitle}) {
    QVERIFY(
        parseDisplayExpression(defaultDisplayExpression(kind), registry_).ok());
  }
}

void TestDisplayExpression::settings_blankMeansDefaultAndValuesAreTrimmed() {
  const QString &statusDefault =
      defaultDisplayExpression(DisplayExpressionKind::StatusBar);
  QCOMPARE(loadDisplayExpressionSetting(DisplayExpressionKind::StatusBar),
           statusDefault);
  QCOMPARE(saveDisplayExpressionSetting(DisplayExpressionKind::StatusBar,
                                        QStringLiteral("   ")),
           statusDefault);
  QCOMPARE(QSettings().value("status_bar/expression").toString(),
           statusDefault);

  QCOMPARE(saveDisplayExpressionSetting(DisplayExpressionKind::WindowTitle,
                                        QStringLiteral("  title  ")),
           QStringLiteral("title"));
  QCOMPARE(QSettings().value("window_title/expression").toString(),
           QStringLiteral("title"));
  QCOMPARE(loadDisplayExpressionSetting(DisplayExpressionKind::WindowTitle),
           QStringLiteral("title"));
}

void TestDisplayExpression::evaluate_runtimeFieldsWinOverSongFields() {
  StatusRuntimeSymbolTable runtime;
  runtime.setBitrateKbps(320);
  MSong song;
  song.insert_or_assign("bitrate", FieldValue("192", "bitrate"));
  song.insert_or_assign("title", FieldValue("Title", "title"));

  // `bitrate` is a runtime field; `title` (builtin:title) is read from the
  // song; a qualified builtin name still reaches the song's value.
  QCOMPARE(evaluate(QStringLiteral("`${bitrate} ${title} ${builtin:bitrate}`"),
                    runtime, &song),
           QStringLiteral("320 Title 192"));
}

void TestDisplayExpression::evaluate_withoutSong_songFieldsAreEmpty() {
  StatusRuntimeSymbolTable runtime;
  runtime.setBitrateKbps(320);
  QCOMPARE(evaluate(QStringLiteral("`${bitrate}|${title}`"), runtime, nullptr),
           QStringLiteral("320|"));
}

QTEST_MAIN(TestDisplayExpression)
#include "tst_displayexpression.moc"
