#include <QObject>
#include <QTest>

#include "../columnregistry.h"
#include "../libraryexpression.h"
#include "../libraryexpression_tokenizer.h"

namespace {
ExprOperatorPtr makeIsOperator() { return std::make_unique<IsOperator>(); }

ExprOperatorPtr makeHasOperator() { return std::make_unique<HasOperator>(); }

ExprOperatorPtr makeInOperator() { return std::make_unique<InOperator>(); }

ExprOperatorPtr makeLtOperator() { return std::make_unique<LtOperator>(); }

ExprOperatorPtr makeLteOperator() { return std::make_unique<LteOperator>(); }

ExprOperatorPtr makeGtOperator() { return std::make_unique<GtOperator>(); }

ExprOperatorPtr makeGteOperator() { return std::make_unique<GteOperator>(); }

ExprPtr makeComparison(std::string exprFieldName, std::string resolvedColumnId,
                       std::string valueText,
                       ExprOperatorPtr op = makeIsOperator(),
                       ColumnValueType valueType = ColumnValueType::Text) {
  ExprValue value;
  value.kind = ExprValue::Kind::Scalar;
  value.values.push_back(std::move(valueText));
  return std::make_unique<ComparisonExpr>(
      ExprFieldRef{std::move(exprFieldName), std::move(resolvedColumnId),
                   valueType},
      std::move(op), std::move(value));
}

ExprPtr makeListComparison(std::string exprFieldName,
                           std::string resolvedColumnId,
                           std::vector<std::string> values, ExprOperatorPtr op,
                           ColumnValueType valueType = ColumnValueType::Text) {
  ExprValue value;
  value.kind = ExprValue::Kind::List;
  value.values = std::move(values);
  return std::make_unique<ComparisonExpr>(
      ExprFieldRef{std::move(exprFieldName), std::move(resolvedColumnId),
                   valueType},
      std::move(op), std::move(value));
}

ExprPtr makeRangeComparison(std::string exprFieldName,
                            std::string resolvedColumnId,
                            std::string startValue, std::string endValue,
                            ExprOperatorPtr op, ColumnValueType valueType) {
  ExprValue value;
  value.kind = ExprValue::Kind::Range;
  value.values = {std::move(startValue), std::move(endValue)};
  return std::make_unique<ComparisonExpr>(
      ExprFieldRef{std::move(exprFieldName), std::move(resolvedColumnId),
                   valueType},
      std::move(op), std::move(value));
}

ExprPtr makeAnd(ExprPtr left, ExprPtr right) {
  return std::make_unique<AndExpr>(std::move(left), std::move(right));
}

ExprPtr makeOr(ExprPtr left, ExprPtr right) {
  return std::make_unique<OrExpr>(std::move(left), std::move(right));
}

ExprPtr makeNot(ExprPtr child) {
  return std::make_unique<NotExpr>(std::move(child));
}

ExprToken makeToken(ExprTokenKind kind, std::string text, int start, int end) {
  return ExprToken{kind, std::move(text), start, end};
}
} // namespace

class TestLibraryExpression : public QObject {
  Q_OBJECT

private slots:
  void tokenize_basicExpression();
  void tokenize_quotedList();
  void tokenize_unterminatedQuotedValue();
  void parse_singleClause();
  void parse_equalsAlias();
  void parse_multiWordValue();
  void parse_quotedScalarValue();
  void parse_quotedListValue();
  void parse_andOrPrecedence();
  void parse_parenthesesOverridePrecedence();
  void parse_caseInsensitiveKeywordsAndFields();
  void parse_customFieldByBareKey();
  void parse_hasOperator();
  void parse_inOperatorList();
  void parse_inOperatorRange();
  void parse_numericValueForNumericField();
  void parse_relationalOperators();
  void parse_notOperator();
  void reject_unknownField();
  void reject_computedField();
  void reject_missingOperatorOrValue();
  void reject_trailingBooleanOperator();
  void reject_missingClosingParenthesis();
  void reject_ambiguousKeywordInValue();
  void reject_unterminatedQuotedValue();
  void reject_unsupportedComparisonForms();
  void reject_invalidTypedValue();
};

void TestLibraryExpression::parse_singleClause() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("artist IS some artist"), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeComparison("artist", "artist", "some artist");
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::tokenize_basicExpression() {
  const std::vector<ExprToken> tokens =
      tokenizeLibraryExpression(QStringLiteral("artist >= 2 AND title = song"));

  const std::vector<ExprToken> expected = {
      makeToken(ExprTokenKind::Identifier, "artist", 0, 6),
      makeToken(ExprTokenKind::OpGte, ">=", 7, 9),
      makeToken(ExprTokenKind::Identifier, "2", 10, 11),
      makeToken(ExprTokenKind::KeywordAnd, "AND", 12, 15),
      makeToken(ExprTokenKind::Identifier, "title", 16, 21),
      makeToken(ExprTokenKind::OpEq, "=", 22, 23),
      makeToken(ExprTokenKind::Identifier, "song", 24, 28),
      makeToken(ExprTokenKind::End, "", 28, 28),
  };

  QCOMPARE(tokens, expected);
}

void TestLibraryExpression::tokenize_quotedList() {
  const std::vector<ExprToken> tokens = tokenizeLibraryExpression(
      QStringLiteral("genre IN [\"rock, pop\", jazz]"));

  const std::vector<ExprToken> expected = {
      makeToken(ExprTokenKind::Identifier, "genre", 0, 5),
      makeToken(ExprTokenKind::KeywordIn, "IN", 6, 8),
      makeToken(ExprTokenKind::LBracket, "[", 9, 10),
      makeToken(ExprTokenKind::StringLiteral, "rock, pop", 10, 21),
      makeToken(ExprTokenKind::Comma, ",", 21, 22),
      makeToken(ExprTokenKind::Identifier, "jazz", 23, 27),
      makeToken(ExprTokenKind::RBracket, "]", 27, 28),
      makeToken(ExprTokenKind::End, "", 28, 28),
  };

  QCOMPARE(tokens, expected);
}

void TestLibraryExpression::tokenize_unterminatedQuotedValue() {
  const QString expressionText = QStringLiteral("title IS \"unfinished");
  const std::vector<ExprToken> tokens =
      tokenizeLibraryExpression(expressionText);

  const std::vector<ExprToken> expected = {
      makeToken(ExprTokenKind::Identifier, "title", 0, 5),
      makeToken(ExprTokenKind::KeywordIs, "IS", 6, 8),
      makeToken(ExprTokenKind::Invalid, "\"unfinished", 9,
                expressionText.size()),
      makeToken(ExprTokenKind::End, "", expressionText.size(),
                expressionText.size()),
  };

  QCOMPARE(tokens.size(), expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    QCOMPARE(tokens[i].kind, expected[i].kind);
    QCOMPARE(tokens[i].text, expected[i].text);
    QCOMPARE(tokens[i].start, expected[i].start);
    QCOMPARE(tokens[i].end, expected[i].end);
  }
}

void TestLibraryExpression::parse_equalsAlias() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("artist = some artist"), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeComparison("artist", "artist", "some artist");
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_multiWordValue() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("title IS the dark side"), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeComparison("title", "title", "the dark side");
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_quotedScalarValue() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("title IS \"rock, pop suite\""), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeComparison("title", "title", "rock, pop suite");
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_quotedListValue() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("genre IN [\"rock, pop\", jazz]"), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeListComparison("genre", "genre", {"rock, pop", "jazz"},
                                        makeInOperator());
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_andOrPrecedence() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("artist IS a OR genre IS pop AND title IS x"), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeOr(makeComparison("artist", "artist", "a"),
                            makeAnd(makeComparison("genre", "genre", "pop"),
                                    makeComparison("title", "title", "x")));
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_parenthesesOverridePrecedence() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("artist IS a AND (genre IS pop OR title IS x)"), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeAnd(makeComparison("artist", "artist", "a"),
                             makeOr(makeComparison("genre", "genre", "pop"),
                                    makeComparison("title", "title", "x")));
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_caseInsensitiveKeywordsAndFields() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("ArTiSt is Some Artist"), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeComparison("artist", "artist", "Some Artist");
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_customFieldByBareKey() {
  ColumnRegistry registry;
  registry.addOrUpdateDynamicColumn(
      {"attr:musicbrainz_trackid", "MusicBrainz Track ID",
       ColumnSource::SongAttribute, ColumnValueType::Text, true, true, 140});

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("musicbrainz_trackid IS abc123"), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeComparison("musicbrainz_trackid",
                                    "attr:musicbrainz_trackid", "abc123");
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_hasOperator() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("genre HAS rock"), registry);

  QVERIFY(result.ok());
  ExprPtr expected =
      makeComparison("genre", "genre", "rock", makeHasOperator());
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_inOperatorList() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("genre IN [rock, pop, jazz]"), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeListComparison(
      "genre", "genre", {"rock", "pop", "jazz"}, makeInOperator());
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_inOperatorRange() {
  ColumnRegistry registry;

  ExprParseResult numericResult =
      parseLibraryExpression(QStringLiteral("tracknumber IN [1..5]"), registry);
  QVERIFY(numericResult.ok());
  ExprPtr numericExpected =
      makeRangeComparison("tracknumber", "tracknumber", "1", "5",
                          makeInOperator(), ColumnValueType::Number);
  QVERIFY(*numericResult.expr == *numericExpected);

  ExprParseResult dateResult = parseLibraryExpression(
      QStringLiteral("date IN [2024-01-01..2025-12-31]"), registry);
  QVERIFY(dateResult.ok());
  ExprPtr dateExpected =
      makeRangeComparison("date", "date", "2024-01-01", "2025-12-31",
                          makeInOperator(), ColumnValueType::DateTime);
  QVERIFY(*dateResult.expr == *dateExpected);
}

void TestLibraryExpression::parse_numericValueForNumericField() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("tracknumber = 2"), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeComparison("tracknumber", "tracknumber", "2",
                                    makeIsOperator(), ColumnValueType::Number);
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_relationalOperators() {
  ColumnRegistry registry;

  ExprParseResult ltResult =
      parseLibraryExpression(QStringLiteral("tracknumber < 2"), registry);
  QVERIFY(ltResult.ok());
  ExprPtr expectedLt =
      makeComparison("tracknumber", "tracknumber", "2", makeLtOperator(),
                     ColumnValueType::Number);
  QVERIFY(*ltResult.expr == *expectedLt);

  ExprParseResult lteResult =
      parseLibraryExpression(QStringLiteral("tracknumber <= 2"), registry);
  QVERIFY(lteResult.ok());
  ExprPtr expectedLte =
      makeComparison("tracknumber", "tracknumber", "2", makeLteOperator(),
                     ColumnValueType::Number);
  QVERIFY(*lteResult.expr == *expectedLte);

  ExprParseResult gtResult =
      parseLibraryExpression(QStringLiteral("tracknumber > 2"), registry);
  QVERIFY(gtResult.ok());
  ExprPtr expectedGt =
      makeComparison("tracknumber", "tracknumber", "2", makeGtOperator(),
                     ColumnValueType::Number);
  QVERIFY(*gtResult.expr == *expectedGt);

  ExprParseResult gteResult =
      parseLibraryExpression(QStringLiteral("tracknumber >= 2"), registry);
  QVERIFY(gteResult.ok());
  ExprPtr expectedGte =
      makeComparison("tracknumber", "tracknumber", "2", makeGteOperator(),
                     ColumnValueType::Number);
  QVERIFY(*gteResult.expr == *expectedGte);
}

void TestLibraryExpression::parse_notOperator() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("NOT genre HAS rock AND title IS song 1"), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeAnd(
      makeNot(makeComparison("genre", "genre", "rock", makeHasOperator())),
      makeComparison("title", "title", "song 1"));
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::reject_unknownField() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("unknown IS value"), registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("Unknown field"));
}

void TestLibraryExpression::reject_computedField() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("status IS playing"), registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("not searchable"));
}

void TestLibraryExpression::reject_missingOperatorOrValue() {
  ColumnRegistry registry;

  ExprParseResult missingOperator =
      parseLibraryExpression(QStringLiteral("artist pop"), registry);
  QVERIFY(!missingOperator.ok());
  QVERIFY(missingOperator.error.message.contains("Expected"));
  QVERIFY(missingOperator.error.message.contains("operator"));

  ExprParseResult missingValue =
      parseLibraryExpression(QStringLiteral("artist IS"), registry);
  QVERIFY(!missingValue.ok());
  QVERIFY(missingValue.error.message.contains("Expected a value"));
}

void TestLibraryExpression::reject_trailingBooleanOperator() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("artist IS a AND"), registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("Expected a field name"));
}

void TestLibraryExpression::reject_missingClosingParenthesis() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("artist IS a AND (genre IS pop OR title IS x"), registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("Expected `)`"));
}

void TestLibraryExpression::reject_ambiguousKeywordInValue() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("artist IS hall AND oates"), registry);

  QVERIFY(!result.ok());
  QVERIFY(!result.error.message.isEmpty());
  QVERIFY(result.error.position >= 0);
}

void TestLibraryExpression::reject_unterminatedQuotedValue() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("title IS \"unfinished"), registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("Unterminated quoted string"));
}

void TestLibraryExpression::reject_unsupportedComparisonForms() {
  ColumnRegistry registry;

  ExprParseResult scalarInResult =
      parseLibraryExpression(QStringLiteral("genre IN pop"), registry);
  QVERIFY(!scalarInResult.ok());
  QVERIFY(scalarInResult.error.message.contains("does not support"));

  ExprParseResult listIsResult =
      parseLibraryExpression(QStringLiteral("genre IS [pop, rock]"), registry);
  QVERIFY(!listIsResult.ok());
  QVERIFY(listIsResult.error.message.contains("does not support"));

  ExprParseResult listHasResult =
      parseLibraryExpression(QStringLiteral("genre HAS [pop, rock]"), registry);
  QVERIFY(!listHasResult.ok());
  QVERIFY(listHasResult.error.message.contains("does not support"));
}

void TestLibraryExpression::reject_invalidTypedValue() {
  ColumnRegistry registry;

  ExprParseResult invalidNumber =
      parseLibraryExpression(QStringLiteral("tracknumber = abc"), registry);
  QVERIFY(!invalidNumber.ok());
  QVERIFY(invalidNumber.error.message.contains("not valid"));

  ExprParseResult invalidNumberList = parseLibraryExpression(
      QStringLiteral("tracknumber IN [1, two]"), registry);
  QVERIFY(!invalidNumberList.ok());
  QVERIFY(invalidNumberList.error.message.contains("not valid"));

  ExprParseResult invalidRangeType =
      parseLibraryExpression(QStringLiteral("genre IN [a..z]"), registry);
  QVERIFY(!invalidRangeType.ok());
  QVERIFY(invalidRangeType.error.message.contains("Range values are only"));

  ExprParseResult invalidRangeOrder = parseLibraryExpression(
      QStringLiteral("tracknumber IN [10..2]"), registry);
  QVERIFY(!invalidRangeOrder.ok());
  QVERIFY(invalidRangeOrder.error.message.contains("Range start"));

  ExprParseResult invalidRangeEndpoint = parseLibraryExpression(
      QStringLiteral("tracknumber IN [1..two]"), registry);
  QVERIFY(!invalidRangeEndpoint.ok());
  QVERIFY(invalidRangeEndpoint.error.message.contains("not valid"));
}

QTEST_MAIN(TestLibraryExpression)
#include "tst_libraryexpression.moc"
