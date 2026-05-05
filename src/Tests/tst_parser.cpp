#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <QTest>

#include "../lyricsloader.h"
#include "../songparser.h"

class TestParser : public QObject {
  Q_OBJECT
public:
  explicit TestParser(QObject *parent = nullptr);
  ColumnRegistry columnRegistry;

private slots:
  void parseRepositoryInputFiles();
  void parseLyricsText_parsesLrcLines();
  void parseLyricsText_handlesInvalidInput();
};

TestParser::TestParser(QObject *parent) : QObject{parent} {}

void TestParser::parseRepositoryInputFiles() {
  const QString inputDirPath = QFINDTESTDATA("parser_inputs");
  QVERIFY2(!inputDirPath.isEmpty(),
           "Missing test input directory: src/Tests/parser_inputs");

  const QString configPath = QDir(inputDirPath).filePath("parser_cases.json");
  QFile configFile(configPath);
  QVERIFY2(
      configFile.exists(),
      "Missing parser config file: src/Tests/parser_inputs/parser_cases.json");
  QVERIFY2(configFile.open(QIODevice::ReadOnly),
           "Failed to open parser config file");

  QJsonParseError parseError;
  const QJsonDocument doc =
      QJsonDocument::fromJson(configFile.readAll(), &parseError);
  QVERIFY2(parseError.error == QJsonParseError::NoError,
           qPrintable(QString("Invalid parser config JSON: %1 at offset %2")
                          .arg(parseError.errorString())
                          .arg(parseError.offset)));
  QVERIFY2(doc.isObject(), "Parser config root must be a JSON object");

  const QJsonObject root = doc.object();
  const QJsonArray cases = root.value("cases").toArray();
  QVERIFY2(!cases.isEmpty(),
           "parser_cases.json has no cases. Add at least one test case.");

  for (int i = 0; i < cases.size(); ++i) {
    QVERIFY2(cases[i].isObject(), "Each case must be a JSON object");
    const QJsonObject caseObj = cases[i].toObject();

    const QString relFile = caseObj.value("file").toString();
    QVERIFY2(!relFile.isEmpty(),
             qPrintable(QString("Case %1 missing 'file'").arg(i)));

    const QString absFile =
        QFileInfo(QDir(inputDirPath).filePath(relFile)).absoluteFilePath();
    QVERIFY2(
        QFileInfo::exists(absFile),
        qPrintable(
            QString("Case %1 input file not found: %2").arg(i).arg(absFile)));

    const QJsonObject expected = caseObj.value("expect").toObject();
    QVERIFY2(!expected.isEmpty(),
             qPrintable(QString("Case %1 has empty 'expect'").arg(i)));

    const MSong song = SongParser::parse(absFile.toStdString(), columnRegistry);

    QVERIFY2(song.contains("filepath"), "Parsed song is missing filepath");
    QCOMPARE(song.at("filepath").text, absFile.toStdString());

    for (auto it = expected.begin(); it != expected.end(); ++it) {
      QVERIFY2(it.value().isString(),
               qPrintable(QString("Case %1 field '%2' expected value must be a "
                                  "string")
                              .arg(i)
                              .arg(it.key())));

      const std::string key = it.key().toStdString();
      QVERIFY2(song.contains(key),
               qPrintable(QString("Case %1 parsed song missing field '%2'")
                              .arg(i)
                              .arg(it.key())));
      QCOMPARE(QString::fromStdString(song.at(key).text),
               it.value().toString());
    }
  }
}

void TestParser::parseLyricsText_parsesLrcLines() {
  const std::string raw = "[ti:Song]\n"
                          "[00:01.00]line 1\n"
                          "[00:02.50][00:03.00]line 2\n";
  const std::map<int, std::string> lyrics = LyricsLoader::parseLyricsText(raw);

  QCOMPARE(lyrics.size(), size_t(3));
  QCOMPARE(lyrics.at(1000), std::string("line 1"));
  QCOMPARE(lyrics.at(2500), std::string("line 2"));
  QCOMPARE(lyrics.at(3000), std::string("line 2"));
}

void TestParser::parseLyricsText_handlesInvalidInput() {
  const std::string raw = "[00:xx.xx]bad\nno timestamp\n";
  const std::map<int, std::string> lyrics = LyricsLoader::parseLyricsText(raw);
  QVERIFY(lyrics.empty());
}

QTEST_MAIN(TestParser)
#include "tst_parser.moc"
