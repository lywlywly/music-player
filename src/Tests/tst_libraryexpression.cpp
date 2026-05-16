#include <QObject>
#include <QTest>
#include <unordered_map>

#include "../columnregistry.h"
#include "../fieldtypepool.h"
#include "../libraryexpression.h"
#include "../libraryexpression_parser.h"
#include "../libraryexpression_tokenizer.h"
#include "../statusruntimesymboltable.h"

namespace {
ExprOperatorPtr makeIsOperator() { return std::make_unique<IsOperator>(); }

ExprOperatorPtr makeHasOperator() { return std::make_unique<HasOperator>(); }

ExprOperatorPtr makeInOperator() { return std::make_unique<InOperator>(); }

ExprOperatorPtr makeLtOperator() { return std::make_unique<LtOperator>(); }

ExprOperatorPtr makeLteOperator() { return std::make_unique<LteOperator>(); }

ExprOperatorPtr makeGtOperator() { return std::make_unique<GtOperator>(); }

ExprOperatorPtr makeGteOperator() { return std::make_unique<GteOperator>(); }

std::string canonicalResolvedId(std::string resolvedColumnId) {
  if (resolvedColumnId.find(':') != std::string::npos) {
    return resolvedColumnId;
  }
  return "builtin:" + resolvedColumnId;
}

ExprPtr makeComparison(std::string exprFieldName, std::string resolvedColumnId,
                       std::string valueText,
                       ExprOperatorPtr op = makeIsOperator(),
                       ValueType valueType = ValueType::Text) {
  ExprValue value;
  value.kind = ExprValue::Kind::Scalar;
  value.values.push_back(std::move(valueText));
  resolvedColumnId = canonicalResolvedId(std::move(resolvedColumnId));
  return std::make_unique<ComparisonExpr>(
      std::make_unique<FieldRefExpr>(ExprFieldRef{
          std::move(exprFieldName), std::move(resolvedColumnId), valueType}),
      std::move(op), std::make_unique<ExprValueExpr>(std::move(value)),
      valueType);
}

ExprPtr makeListComparison(std::string exprFieldName,
                           std::string resolvedColumnId,
                           std::vector<std::string> values, ExprOperatorPtr op,
                           ValueType valueType = ValueType::Text) {
  ExprValue value;
  value.kind = ExprValue::Kind::List;
  value.values = std::move(values);
  resolvedColumnId = canonicalResolvedId(std::move(resolvedColumnId));
  return std::make_unique<ComparisonExpr>(
      std::make_unique<FieldRefExpr>(ExprFieldRef{
          std::move(exprFieldName), std::move(resolvedColumnId), valueType}),
      std::move(op), std::make_unique<ExprValueExpr>(std::move(value)),
      valueType);
}

ExprPtr makeRangeComparison(std::string exprFieldName,
                            std::string resolvedColumnId,
                            std::string startValue, std::string endValue,
                            ExprOperatorPtr op, ValueType valueType) {
  ExprValue value;
  value.kind = ExprValue::Kind::Range;
  value.values = {std::move(startValue), std::move(endValue)};
  resolvedColumnId = canonicalResolvedId(std::move(resolvedColumnId));
  return std::make_unique<ComparisonExpr>(
      std::make_unique<FieldRefExpr>(ExprFieldRef{
          std::move(exprFieldName), std::move(resolvedColumnId), valueType}),
      std::move(op), std::make_unique<ExprValueExpr>(std::move(value)),
      valueType);
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

ExprPtr makeLiteral(std::string text) {
  return std::make_unique<LiteralExpr>(
      ExprRuntimeValue::fromText(std::move(text)));
}

ExprPtr makeBoolLiteral(bool value) {
  return std::make_unique<LiteralExpr>(ExprRuntimeValue::fromBool(value));
}

ExprPtr makeNumberLiteral(double value) {
  return std::make_unique<LiteralExpr>(ExprRuntimeValue::fromNumber(value));
}

ExprPtr makeIf(ExprPtr condition, ExprPtr thenExpr, ExprPtr elseExpr) {
  return std::make_unique<IfExpr>(std::move(condition), std::move(thenExpr),
                                  std::move(elseExpr));
}

ExprPtr makeFieldRefExpr(std::string exprFieldName,
                         std::string resolvedColumnId,
                         ValueType valueType = ValueType::Text) {
  resolvedColumnId = canonicalResolvedId(std::move(resolvedColumnId));
  return std::make_unique<FieldRefExpr>(ExprFieldRef{
      std::move(exprFieldName), std::move(resolvedColumnId), valueType});
}

ExprPtr makeInterpolatedExpr(std::vector<ExprPtr> parts) {
  return std::make_unique<InterpolatedStringExpr>(std::move(parts));
}

ExprPtr makeExprComparison(ExprPtr left, std::string valueText,
                           ExprOperatorPtr op = makeIsOperator()) {
  ExprValue value;
  value.kind = ExprValue::Kind::Scalar;
  value.values.push_back(std::move(valueText));
  const ExprStaticType leftType = inferExprStaticType(*left);
  const ValueType leftValueType =
      leftType == ExprStaticType::Number
          ? ValueType::Number
          : (leftType == ExprStaticType::Bool ? ValueType::Boolean
                                              : ValueType::Text);
  return std::make_unique<ComparisonExpr>(
      std::move(left), std::move(op),
      std::make_unique<ExprValueExpr>(std::move(value)), leftValueType);
}

ExprToken makeToken(ExprTokenKind kind, QString text, int start, int end) {
  return ExprToken{kind, std::move(text), start, end};
}

struct TestEvalContext final : LibraryExprEvalContext {
  std::unordered_map<std::string, FieldValue> values;
  mutable std::unordered_map<std::string, FieldValue> cache;

  const FieldValue *fieldValue(std::string_view fieldId) const override {
    std::string key(fieldId);
    if (fieldId.starts_with("status:")) {
      key = std::string(fieldId.substr(7));
    } else if (fieldId.starts_with("builtin:")) {
      key = std::string(fieldId.substr(8));
    }
    auto it = values.find(key);
    if (it == values.end() && fieldId.starts_with("computed:")) {
      it = values.find(std::string(fieldId.substr(9)));
    }
    if (it == values.end()) {
      return nullptr;
    }
    if (fieldId.find(':') != std::string_view::npos &&
        it->second.fieldId != fieldId) {
      auto [cacheIt, inserted] = cache.insert_or_assign(
          std::string(fieldId),
          FieldValue(it->second.text, std::string(fieldId)));
      Q_UNUSED(inserted);
      return &cacheIt->second;
    }
    return &it->second;
  }
};

struct DurationDisplayEvalContext final : LibraryExprEvalContext {
  std::unordered_map<std::string, FieldValue> values;
  mutable std::unordered_map<std::string, FieldValue> cache;

  DurationDisplayEvalContext() {
    FieldTypePool::instance().upsert(std::vector<FieldDefinition>{
        {.id = QStringLiteral("playback_time"),
         .valueType = ValueType::Number,
         .displayKind = DisplayKind::DurationSeconds},
        {.id = QStringLiteral("duration"),
         .valueType = ValueType::Number,
         .displayKind = DisplayKind::DurationSeconds},
        {.id = QStringLiteral("channels"),
         .valueType = ValueType::Number,
         .displayKind = DisplayKind::ChannelLayout},
    });
  }

  const FieldValue *fieldValue(std::string_view fieldId) const override {
    std::string key(fieldId);
    if (fieldId.starts_with("status:")) {
      key = std::string(fieldId.substr(7));
    } else if (fieldId.starts_with("builtin:")) {
      key = std::string(fieldId.substr(8));
    }
    auto it = values.find(key);
    if (it == values.end() && fieldId.starts_with("computed:")) {
      it = values.find(std::string(fieldId.substr(9)));
    }
    if (it == values.end()) {
      return nullptr;
    }
    if (fieldId.find(':') != std::string_view::npos &&
        it->second.fieldId != fieldId) {
      auto [cacheIt, inserted] = cache.insert_or_assign(
          std::string(fieldId),
          FieldValue(it->second.text, std::string(fieldId)));
      Q_UNUSED(inserted);
      return &cacheIt->second;
    }
    return &it->second;
  }
};

struct RuntimeFirstEvalContext final : LibraryExprEvalContext {
  std::unordered_map<std::string, FieldValue> runtimeValues;
  std::unordered_map<std::string, FieldValue> songValues;
  mutable std::unordered_map<std::string, FieldValue> cache;

  const FieldValue *fieldValue(std::string_view fieldId) const override {
    if (fieldId.starts_with("status:")) {
      auto runtimeIt = runtimeValues.find(std::string(fieldId));
      if (runtimeIt != runtimeValues.end()) {
        return &runtimeIt->second;
      }
    }
    std::string key(fieldId);
    if (fieldId.starts_with("status:")) {
      key = std::string(fieldId.substr(7));
    } else if (fieldId.starts_with("builtin:")) {
      key = std::string(fieldId.substr(8));
    }

    auto runtimeIt = runtimeValues.find(key);
    if (runtimeIt != runtimeValues.end()) {
      return &runtimeIt->second;
    }
    auto songIt = songValues.find(key);
    if (songIt == songValues.end() && fieldId.starts_with("computed:")) {
      songIt = songValues.find(std::string(fieldId.substr(9)));
    }
    if (songIt == songValues.end()) {
      return nullptr;
    }
    if (fieldId.find(':') != std::string_view::npos &&
        songIt->second.fieldId != fieldId) {
      auto [cacheIt, inserted] = cache.insert_or_assign(
          std::string(fieldId),
          FieldValue(songIt->second.text, std::string(fieldId)));
      Q_UNUSED(inserted);
      return &cacheIt->second;
    }
    return &songIt->second;
  }
};

ExprParseResult parseLibraryExpression(const QString &expressionText,
                                       const ColumnRegistry &registry) {
  const ExprSymbolResolver resolver(registry.expressionSymbols());
  return ::parseLibraryExpression(expressionText, resolver);
}

ExprParseResult
parseLibraryExpression(const QString &expressionText,
                       const ColumnRegistry &registry,
                       const std::vector<ExprSymbolInfo> &runtimeSymbols) {
  const ExprSymbolResolver resolver(
      mergeExprSymbols(std::vector<ExprSymbolInfo>(runtimeSymbols),
                       registry.expressionSymbols()));
  return ::parseLibraryExpression(expressionText, resolver);
}
} // namespace

class TestLibraryExpression : public QObject {
  Q_OBJECT

private slots:
  void tokenize_basicExpression();
  void tokenize_quotedList();
  void tokenize_ifThenElse();
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
  void parse_ifThenElse();
  void parse_ifThenElseBooleanBranch();
  void parse_ifThenElseNumericBranch();
  void parse_interpolatedStringLiteral();
  void parse_interpolatedStringLiteral_nested();
  void reject_interpolatedStringLiteral_unquotedNested();
  void parse_runtimeSymbolField_withSymbols();
  void parse_namespacedRuntimeField_withSymbols();
  void parse_namespacedBuiltinField();
  void parse_namespacedAttrField();
  void parse_namespacedComputedField();
  void parse_unqualifiedComputedField_resolvesQualified();
  void parse_runtimeSymbolField_rejectsLengthAlias();
  void runtimeSymbolTable_registersFieldTypesInPool();
  void parse_defaultStatusBarExpression_withRuntimeSymbols();
  void parse_defaultWindowTitleExpression_withRuntimeSymbols();
  void evaluate_ifThenElse();
  void evaluate_ifThenElseBooleanBranch();
  void evaluate_ifThenElseNumericBranch();
  void evaluate_interpolatedStringLiteral();
  void evaluate_interpolatedStringLiteral_nested();
  void evaluate_runtimeDurationInterpolation_usesDurationDisplay();
  void evaluate_channelsInterpolation_usesChannelDisplay();
  void evaluate_runtimeFieldCollision_runtimeWins();
  void evaluate_attrVsComputedCollision_unqualifiedPrefersAttr();
  void evaluate_builtinVsAttrCollision_unqualifiedPrefersBuiltin();
  void evaluate_ifExprUsedByAnd();
  void evaluate_ifExprUsedByOr();
  void evaluate_ifExprUsedByNot();
  void parse_ifExprComparedWithIsText();
  void evaluate_ifExprComparedWithIsText();
  void evaluate_ifExprComparedWithIsNumber();
  void evaluate_ifExprComparedWithIsBool();
  void evaluate_ifExprComparedWithHasText();
  void evaluate_ifExprComparedWithInList();
  void evaluate_leftRuntimeExprWithoutFieldLookup();
  void evaluate_ifExprSupportsAllTextOperators();
  void evaluate_ifExprSupportsAllNumberOperators();
  void evaluate_nestedIfHasComparedWithIsBool();
  void reject_ifExprComparedWithIsDifferentType();
  void reject_ifWithNonBooleanCondition();
  void reject_ifWithMismatchedBranchTypes();
  void reject_nestedBooleanOpInIfBranch();
  void reject_unknownField();
  void reject_internalAttrKeyInExpression();
  void reject_runtimeSymbolInDefaultParseContext();
  void reject_computedField();
  void reject_missingOperatorOrValue();
  void reject_trailingBooleanOperator();
  void reject_missingClosingParenthesis();
  void reject_ambiguousKeywordInValue();
  void reject_andWithNonBooleanOperand();
  void reject_orWithNonBooleanOperand();
  void reject_notWithNonBooleanOperand();
  void reject_unterminatedQuotedValue();
  void reject_unterminatedInterpolationPlaceholder();
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

void TestLibraryExpression::tokenize_ifThenElse() {
  const std::vector<ExprToken> tokens = tokenizeLibraryExpression(
      QStringLiteral("IF artist IS a THEN \"lossless\" ELSE \"lossy\""));

  const std::vector<ExprToken> expected = {
      makeToken(ExprTokenKind::KeywordIf, "IF", 0, 2),
      makeToken(ExprTokenKind::Identifier, "artist", 3, 9),
      makeToken(ExprTokenKind::KeywordIs, "IS", 10, 12),
      makeToken(ExprTokenKind::Identifier, "a", 13, 14),
      makeToken(ExprTokenKind::KeywordThen, "THEN", 15, 19),
      makeToken(ExprTokenKind::StringLiteral, "lossless", 20, 30),
      makeToken(ExprTokenKind::KeywordElse, "ELSE", 31, 35),
      makeToken(ExprTokenKind::StringLiteral, "lossy", 36, 43),
      makeToken(ExprTokenKind::End, "", 43, 43),
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
  registry.addOrUpdateDynamicColumn({.id = "attr:musicbrainz_trackid",
                                     .title = "MusicBrainz Track ID",
                                     .sortable = true,
                                     .visibleByDefault = true,
                                     .defaultWidth = 140},
                                    {.id = "attr:musicbrainz_trackid",
                                     .source = ColumnSource::SongAttribute,
                                     .valueType = ValueType::Text,
                                     .displayKind = DisplayKind::Raw,
                                     .expression = "",
                                     .searchable = true,
                                     .writable = true});

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
                          makeInOperator(), ValueType::Number);
  QVERIFY(*numericResult.expr == *numericExpected);

  ExprParseResult dateResult = parseLibraryExpression(
      QStringLiteral("date IN [2024-01-01..2025-12-31]"), registry);
  QVERIFY(dateResult.ok());
  ExprPtr dateExpected =
      makeRangeComparison("date", "date", "2024-01-01", "2025-12-31",
                          makeInOperator(), ValueType::DateTime);
  QVERIFY(*dateResult.expr == *dateExpected);
}

void TestLibraryExpression::parse_numericValueForNumericField() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("tracknumber = 2"), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeComparison("tracknumber", "tracknumber", "2",
                                    makeIsOperator(), ValueType::Number);
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_relationalOperators() {
  ColumnRegistry registry;

  ExprParseResult ltResult =
      parseLibraryExpression(QStringLiteral("tracknumber < 2"), registry);
  QVERIFY(ltResult.ok());
  ExprPtr expectedLt = makeComparison("tracknumber", "tracknumber", "2",
                                      makeLtOperator(), ValueType::Number);
  QVERIFY(*ltResult.expr == *expectedLt);

  ExprParseResult lteResult =
      parseLibraryExpression(QStringLiteral("tracknumber <= 2"), registry);
  QVERIFY(lteResult.ok());
  ExprPtr expectedLte = makeComparison("tracknumber", "tracknumber", "2",
                                       makeLteOperator(), ValueType::Number);
  QVERIFY(*lteResult.expr == *expectedLte);

  ExprParseResult gtResult =
      parseLibraryExpression(QStringLiteral("tracknumber > 2"), registry);
  QVERIFY(gtResult.ok());
  ExprPtr expectedGt = makeComparison("tracknumber", "tracknumber", "2",
                                      makeGtOperator(), ValueType::Number);
  QVERIFY(*gtResult.expr == *expectedGt);

  ExprParseResult gteResult =
      parseLibraryExpression(QStringLiteral("tracknumber >= 2"), registry);
  QVERIFY(gteResult.ok());
  ExprPtr expectedGte = makeComparison("tracknumber", "tracknumber", "2",
                                       makeGteOperator(), ValueType::Number);
  QVERIFY(*gteResult.expr == *expectedGte);

  ExprParseResult dateGtResult =
      parseLibraryExpression(QStringLiteral("date > 2024-01-01"), registry);
  QVERIFY(dateGtResult.ok());
  ExprPtr expectedDateGt = makeComparison(
      "date", "date", "2024-01-01", makeGtOperator(), ValueType::DateTime);
  QVERIFY(*dateGtResult.expr == *expectedDateGt);
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

void TestLibraryExpression::parse_ifThenElse() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("IF artist IS a THEN \"lossless\" ELSE \"lossy\""),
      registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeIf(makeComparison("artist", "artist", "a"),
                            makeLiteral("lossless"), makeLiteral("lossy"));
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_ifThenElseBooleanBranch() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("IF artist IS a THEN true ELSE false"), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeIf(makeComparison("artist", "artist", "a"),
                            makeBoolLiteral(true), makeBoolLiteral(false));
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_ifThenElseNumericBranch() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("IF artist IS a THEN 1.5 ELSE 2"), registry);

  QVERIFY(result.ok());
  ExprPtr expected = makeIf(makeComparison("artist", "artist", "a"),
                            makeNumberLiteral(1.5), makeNumberLiteral(2.0));
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_interpolatedStringLiteral() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("`${codec} | ${bitrate} kbps`"), registry);

  QVERIFY(result.ok());
  std::vector<ExprPtr> parts;
  parts.push_back(makeFieldRefExpr("codec", "codec"));
  parts.push_back(makeLiteral(" | "));
  parts.push_back(makeFieldRefExpr("bitrate", "bitrate", ValueType::Number));
  parts.push_back(makeLiteral(" kbps"));
  ExprPtr expected = makeInterpolatedExpr(std::move(parts));
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_interpolatedStringLiteral_nested() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("`${IF bitrate > 0 THEN `${bitrate}` ELSE ''} "
                     "kbps`"),
      registry, StatusRuntimeSymbolTable::expressionSymbols());

  QVERIFY(result.ok());
}

void TestLibraryExpression::reject_interpolatedStringLiteral_unquotedNested() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("`${IF bitrate > 0 THEN ${bitrate} ELSE ''} kbps`"),
      registry, StatusRuntimeSymbolTable::expressionSymbols());
  QVERIFY(!result.ok());
}

void TestLibraryExpression::parse_runtimeSymbolField_withSymbols() {
  ColumnRegistry registry;
  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("isplaying IS true"), registry,
                             StatusRuntimeSymbolTable::expressionSymbols());
  QVERIFY(result.ok());
  ExprPtr expected = makeComparison("isplaying", "status:isplaying", "true",
                                    makeIsOperator(), ValueType::Boolean);
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_namespacedRuntimeField_withSymbols() {
  ColumnRegistry registry;
  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("status:bitrate > 0"), registry,
                             StatusRuntimeSymbolTable::expressionSymbols());
  QVERIFY(result.ok());
  ExprPtr expected = makeComparison("status:bitrate", "status:bitrate", "0",
                                    makeGtOperator(), ValueType::Number);
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_namespacedBuiltinField() {
  ColumnRegistry registry;
  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("builtin:artist IS a"), registry);
  QVERIFY(result.ok());
  ExprPtr expected = makeComparison("builtin:artist", "builtin:artist", "a");
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_namespacedAttrField() {
  ColumnRegistry registry;
  registry.addOrUpdateDynamicColumn({.id = "attr:rating",
                                     .title = "Rating",
                                     .sortable = true,
                                     .visibleByDefault = true,
                                     .defaultWidth = 100},
                                    {.id = "attr:rating",
                                     .source = ColumnSource::SongAttribute,
                                     .valueType = ValueType::Number,
                                     .displayKind = DisplayKind::Raw,
                                     .expression = "",
                                     .searchable = true,
                                     .writable = true});
  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("attr:rating >= 3"), registry);
  QVERIFY(result.ok());
  ExprPtr expected = makeComparison("attr:rating", "attr:rating", "3",
                                    makeGteOperator(), ValueType::Number);
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_namespacedComputedField() {
  ColumnRegistry registry;
  registry.addOrUpdateDynamicColumn({.id = "computed:score",
                                     .title = "Score",
                                     .sortable = true,
                                     .visibleByDefault = true,
                                     .defaultWidth = 100},
                                    {.id = "computed:score",
                                     .source = ColumnSource::Computed,
                                     .valueType = ValueType::Number,
                                     .displayKind = DisplayKind::Raw,
                                     .expression = "1",
                                     .searchable = true,
                                     .writable = false});
  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("computed:score > 10"), registry);
  QVERIFY(result.ok());
  ExprPtr expected = makeComparison("computed:score", "computed:score", "10",
                                    makeGtOperator(), ValueType::Number);
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_unqualifiedComputedField_resolvesQualified() {
  ColumnRegistry registry;
  registry.addOrUpdateDynamicColumn({.id = "computed:score",
                                     .title = "Score",
                                     .sortable = true,
                                     .visibleByDefault = true,
                                     .defaultWidth = 100},
                                    {.id = "computed:score",
                                     .source = ColumnSource::Computed,
                                     .valueType = ValueType::Number,
                                     .displayKind = DisplayKind::Raw,
                                     .expression = "1",
                                     .searchable = true,
                                     .writable = false});
  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("score > 10"), registry);
  QVERIFY(result.ok());
  ExprPtr expected = makeComparison("score", "computed:score", "10",
                                    makeGtOperator(), ValueType::Number);
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::parse_runtimeSymbolField_rejectsLengthAlias() {
  ColumnRegistry registry;
  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("length IS 120"), registry,
                             StatusRuntimeSymbolTable::expressionSymbols());
  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("Unknown field"));
}

void TestLibraryExpression::runtimeSymbolTable_registersFieldTypesInPool() {
  StatusRuntimeSymbolTable runtimeSymbols;
  Q_UNUSED(runtimeSymbols);

  const FieldDefinition *playbackTime =
      FieldTypePool::instance().find("status:playback_time");
  QVERIFY(playbackTime != nullptr);
  QCOMPARE(playbackTime->valueType, ValueType::Number);
  QCOMPARE(playbackTime->displayKind, DisplayKind::DurationSeconds);

  const FieldDefinition *duration =
      FieldTypePool::instance().find("status:duration");
  QVERIFY(duration != nullptr);
  QCOMPARE(duration->valueType, ValueType::Number);
  QCOMPARE(duration->displayKind, DisplayKind::DurationSeconds);

  const FieldDefinition *isPlaying =
      FieldTypePool::instance().find("status:isplaying");
  QVERIFY(isPlaying != nullptr);
  QCOMPARE(isPlaying->valueType, ValueType::Boolean);
}

void TestLibraryExpression::
    parse_defaultStatusBarExpression_withRuntimeSymbols() {
  ColumnRegistry registry;
  ExprParseResult result = parseLibraryExpression(
      StatusRuntimeSymbolTable::defaultStatusBarExpression(), registry,
      StatusRuntimeSymbolTable::expressionSymbols());
  QVERIFY(result.ok());
}

void TestLibraryExpression::
    parse_defaultWindowTitleExpression_withRuntimeSymbols() {
  ColumnRegistry registry;
  ExprParseResult result = parseLibraryExpression(
      StatusRuntimeSymbolTable::defaultWindowTitleExpression(), registry,
      StatusRuntimeSymbolTable::expressionSymbols());
  QVERIFY(result.ok());
}

void TestLibraryExpression::evaluate_ifThenElse() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("IF artist IS a THEN \"lossless\" ELSE \"lossy\""),
      registry);
  QVERIFY(result.ok());

  TestEvalContext context;
  context.values.emplace("artist", FieldValue("a", "artist"));

  ExprRuntimeValue trueValue = result.expr->evaluateValue(context);
  QVERIFY(trueValue.isText());
  QCOMPARE(QString::fromStdString(trueValue.textValue()),
           QStringLiteral("lossless"));

  context.values.insert_or_assign("artist", FieldValue("b", "artist"));
  ExprRuntimeValue falseValue = result.expr->evaluateValue(context);
  QVERIFY(falseValue.isText());
  QCOMPARE(QString::fromStdString(falseValue.textValue()),
           QStringLiteral("lossy"));
}

void TestLibraryExpression::evaluate_ifThenElseBooleanBranch() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("IF artist IS a THEN true ELSE false"), registry);
  QVERIFY(result.ok());

  TestEvalContext context;
  context.values.emplace("artist", FieldValue("a", "artist"));

  ExprRuntimeValue trueValue = result.expr->evaluateValue(context);
  QVERIFY(trueValue.isBool());
  QVERIFY(trueValue.boolValueOrFalse());

  context.values.insert_or_assign("artist", FieldValue("b", "artist"));
  ExprRuntimeValue falseValue = result.expr->evaluateValue(context);
  QVERIFY(falseValue.isBool());
  QVERIFY(!falseValue.boolValueOrFalse());
}

void TestLibraryExpression::evaluate_ifThenElseNumericBranch() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("IF artist IS a THEN 1.5 ELSE 2"), registry);
  QVERIFY(result.ok());

  TestEvalContext context;
  context.values.emplace("artist", FieldValue("a", "artist"));

  ExprRuntimeValue trueValue = result.expr->evaluateValue(context);
  QVERIFY(trueValue.isNumber());
  QCOMPARE(trueValue.numberValue(), 1.5);

  context.values.insert_or_assign("artist", FieldValue("b", "artist"));
  ExprRuntimeValue falseValue = result.expr->evaluateValue(context);
  QVERIFY(falseValue.isNumber());
  QCOMPARE(falseValue.numberValue(), 2.0);
}

void TestLibraryExpression::evaluate_interpolatedStringLiteral() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("`${codec} | ${bitrate} kbps`"), registry);
  QVERIFY(result.ok());

  TestEvalContext context;
  context.values.emplace("codec", FieldValue("mp3", "codec"));
  context.values.emplace("bitrate", FieldValue("320", "bitrate"));

  ExprRuntimeValue value = result.expr->evaluateValue(context);
  QVERIFY(value.isText());
  QCOMPARE(QString::fromStdString(value.textValue()),
           QStringLiteral("mp3 | 320 kbps"));
}

void TestLibraryExpression::evaluate_interpolatedStringLiteral_nested() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("`${IF bitrate > 0 THEN `${bitrate}` ELSE ''} "
                     "kbps`"),
      registry, StatusRuntimeSymbolTable::expressionSymbols());
  QVERIFY(result.ok());

  TestEvalContext context;
  context.values.emplace("bitrate", FieldValue("320", "bitrate"));

  ExprRuntimeValue value = result.expr->evaluateValue(context);
  QVERIFY(value.isText());
  QCOMPARE(QString::fromStdString(value.textValue()),
           QStringLiteral("320 kbps"));
}

void TestLibraryExpression::
    evaluate_runtimeDurationInterpolation_usesDurationDisplay() {
  ColumnRegistry registry;
  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("`${playback_time} / ${duration}`"), registry,
      StatusRuntimeSymbolTable::expressionSymbols());
  QVERIFY(result.ok());

  DurationDisplayEvalContext context;
  context.values.emplace("playback_time", FieldValue("65", "playback_time"));
  context.values.emplace("duration", FieldValue("125", "duration"));

  ExprRuntimeValue value = result.expr->evaluateValue(context);
  QVERIFY(value.isText());
  QCOMPARE(QString::fromStdString(value.textValue()),
           QStringLiteral("01:05 / 02:05"));
}

void TestLibraryExpression::
    evaluate_channelsInterpolation_usesChannelDisplay() {
  ColumnRegistry registry;
  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("`${channels}`"), registry);
  QVERIFY(result.ok());

  DurationDisplayEvalContext context;
  context.values.emplace("channels", FieldValue("2", "channels"));

  ExprRuntimeValue value = result.expr->evaluateValue(context);
  QVERIFY(value.isText());
  QCOMPARE(QString::fromStdString(value.textValue()), QStringLiteral("stereo"));

  context.values.insert_or_assign("channels", FieldValue("1", "channels"));
  value = result.expr->evaluateValue(context);
  QVERIFY(value.isText());
  QCOMPARE(QString::fromStdString(value.textValue()), QStringLiteral("mono"));

  context.values.insert_or_assign("channels", FieldValue("6", "channels"));
  value = result.expr->evaluateValue(context);
  QVERIFY(value.isText());
  QCOMPARE(QString::fromStdString(value.textValue()), QStringLiteral("6"));
}

void TestLibraryExpression::evaluate_runtimeFieldCollision_runtimeWins() {
  ColumnRegistry registry;
  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("bitrate IS 320"), registry,
                             StatusRuntimeSymbolTable::expressionSymbols());
  QVERIFY(result.ok());

  RuntimeFirstEvalContext context;
  context.songValues.emplace("bitrate", FieldValue("192", "bitrate"));
  context.runtimeValues.emplace("status:bitrate",
                                FieldValue("320", "status:bitrate"));

  QVERIFY(result.expr->evaluate(context));
}

void TestLibraryExpression::
    evaluate_attrVsComputedCollision_unqualifiedPrefersAttr() {
  ColumnRegistry registry;
  registry.addOrUpdateDynamicColumn({.id = "attr:score",
                                     .title = "Score(tag)",
                                     .sortable = true,
                                     .visibleByDefault = true,
                                     .defaultWidth = 100},
                                    {.id = "attr:score",
                                     .source = ColumnSource::SongAttribute,
                                     .valueType = ValueType::Number,
                                     .displayKind = DisplayKind::Raw,
                                     .expression = "",
                                     .searchable = true,
                                     .writable = true});
  registry.addOrUpdateDynamicColumn({.id = "computed:score",
                                     .title = "Score(computed)",
                                     .sortable = true,
                                     .visibleByDefault = true,
                                     .defaultWidth = 100},
                                    {.id = "computed:score",
                                     .source = ColumnSource::Computed,
                                     .valueType = ValueType::Number,
                                     .displayKind = DisplayKind::Raw,
                                     .expression = "1",
                                     .searchable = true,
                                     .writable = false});

  ExprParseResult unqualified =
      parseLibraryExpression(QStringLiteral("score > 10"), registry);
  QVERIFY(unqualified.ok());

  ExprParseResult qualified =
      parseLibraryExpression(QStringLiteral("computed:score > 10"), registry);
  QVERIFY(qualified.ok());

  TestEvalContext context;
  context.values.emplace("attr:score", FieldValue("20", "attr:score"));
  context.values.emplace("computed:score", FieldValue("5", "computed:score"));

  QVERIFY(unqualified.expr->evaluate(context));
  QVERIFY(!qualified.expr->evaluate(context));
}

void TestLibraryExpression::
    evaluate_builtinVsAttrCollision_unqualifiedPrefersBuiltin() {
  ColumnRegistry registry;
  registry.addOrUpdateDynamicColumn({.id = "attr:bitrate",
                                     .title = "Bitrate(tag)",
                                     .sortable = true,
                                     .visibleByDefault = true,
                                     .defaultWidth = 100},
                                    {.id = "attr:bitrate",
                                     .source = ColumnSource::SongAttribute,
                                     .valueType = ValueType::Number,
                                     .displayKind = DisplayKind::Raw,
                                     .expression = "",
                                     .searchable = true,
                                     .writable = true});

  ExprParseResult unqualified =
      parseLibraryExpression(QStringLiteral("bitrate IS 192"), registry);
  QVERIFY(unqualified.ok());

  ExprParseResult qualified =
      parseLibraryExpression(QStringLiteral("attr:bitrate IS 320"), registry);
  QVERIFY(qualified.ok());

  TestEvalContext context;
  context.values.emplace("bitrate", FieldValue("192", "bitrate"));
  context.values.emplace("attr:bitrate", FieldValue("320", "attr:bitrate"));

  QVERIFY(unqualified.expr->evaluate(context));
  QVERIFY(qualified.expr->evaluate(context));
}

void TestLibraryExpression::evaluate_ifExprUsedByAnd() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("(IF artist IS a THEN true ELSE false) AND genre IS rock"),
      registry);
  QVERIFY(result.ok());

  TestEvalContext context;
  context.values.emplace("artist", FieldValue("a", "artist"));
  context.values.emplace("genre", FieldValue("rock", "genre"));
  QVERIFY(result.expr->evaluate(context));

  context.values.insert_or_assign("artist", FieldValue("b", "artist"));
  QVERIFY(!result.expr->evaluate(context));
}

void TestLibraryExpression::evaluate_ifExprUsedByOr() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("(IF artist IS a THEN true ELSE false) OR genre IS rock"),
      registry);
  QVERIFY(result.ok());

  TestEvalContext context;
  context.values.emplace("artist", FieldValue("b", "artist"));
  context.values.emplace("genre", FieldValue("jazz", "genre"));
  QVERIFY(!result.expr->evaluate(context));

  context.values.insert_or_assign("artist", FieldValue("a", "artist"));
  QVERIFY(result.expr->evaluate(context));
}

void TestLibraryExpression::evaluate_ifExprUsedByNot() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("NOT (IF artist IS a THEN true ELSE false)"), registry);
  QVERIFY(result.ok());

  TestEvalContext context;
  context.values.emplace("artist", FieldValue("a", "artist"));
  QVERIFY(!result.expr->evaluate(context));

  context.values.insert_or_assign("artist", FieldValue("b", "artist"));
  QVERIFY(result.expr->evaluate(context));
}

void TestLibraryExpression::parse_ifExprComparedWithIsText() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("(IF artist IS a THEN b/c ELSE c) IS b/c"), registry);
  QVERIFY(result.ok());

  ExprPtr expected =
      makeExprComparison(makeIf(makeComparison("artist", "artist", "a"),
                                makeLiteral("b/c"), makeLiteral("c")),
                         "b/c");
  QVERIFY(*result.expr == *expected);
}

void TestLibraryExpression::evaluate_ifExprComparedWithIsText() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("(IF artist IS a THEN b/c ELSE c) IS b/c"), registry);
  QVERIFY(result.ok());

  TestEvalContext context;
  context.values.emplace("artist", FieldValue("a", "artist"));
  QVERIFY(result.expr->evaluate(context));

  context.values.insert_or_assign("artist", FieldValue("b", "artist"));
  QVERIFY(!result.expr->evaluate(context));
}

void TestLibraryExpression::evaluate_ifExprComparedWithIsNumber() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("(IF artist IS a THEN 1 ELSE 2) IS 2"), registry);
  QVERIFY(result.ok());

  TestEvalContext context;
  context.values.emplace("artist", FieldValue("a", "artist"));
  QVERIFY(!result.expr->evaluate(context));

  context.values.insert_or_assign("artist", FieldValue("b", "artist"));
  QVERIFY(result.expr->evaluate(context));
}

void TestLibraryExpression::evaluate_ifExprComparedWithIsBool() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("(IF artist IS a THEN true ELSE false) IS false"),
      registry);
  QVERIFY(result.ok());

  TestEvalContext context;
  context.values.emplace("artist", FieldValue("a", "artist"));
  QVERIFY(!result.expr->evaluate(context));

  context.values.insert_or_assign("artist", FieldValue("b", "artist"));
  QVERIFY(result.expr->evaluate(context));
}

void TestLibraryExpression::evaluate_ifExprComparedWithHasText() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("(IF artist IS a THEN b / c ELSE d / e) HAS b"), registry);
  QVERIFY(result.ok());

  TestEvalContext context;
  context.values.emplace("artist", FieldValue("a", "artist"));
  QVERIFY(result.expr->evaluate(context));

  context.values.insert_or_assign("artist", FieldValue("x", "artist"));
  QVERIFY(!result.expr->evaluate(context));
}

void TestLibraryExpression::evaluate_ifExprComparedWithInList() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("(IF artist IS a THEN 1 ELSE 2) IN [2, 3]"), registry);
  QVERIFY(result.ok());

  TestEvalContext context;
  context.values.emplace("artist", FieldValue("a", "artist"));
  QVERIFY(!result.expr->evaluate(context));

  context.values.insert_or_assign("artist", FieldValue("x", "artist"));
  QVERIFY(result.expr->evaluate(context));
}

void TestLibraryExpression::evaluate_leftRuntimeExprWithoutFieldLookup() {
  ColumnRegistry registry;
  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("(IF true THEN 10 ELSE 20) > 9"), registry);
  QVERIFY(result.ok());

  TestEvalContext context;
  QVERIFY(result.expr->evaluate(context));
}

void TestLibraryExpression::evaluate_ifExprSupportsAllTextOperators() {
  ColumnRegistry registry;
  TestEvalContext context;
  context.values.emplace("artist", FieldValue("a", "artist"));

  const QString base = QStringLiteral("(IF artist IS a THEN b / c ELSE d / e)");

  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" IS b / c"), registry);
    QVERIFY(result.ok());
    QVERIFY(result.expr->evaluate(context));
  }
  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" = b / c"), registry);
    QVERIFY(result.ok());
    QVERIFY(result.expr->evaluate(context));
  }
  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" HAS b"), registry);
    QVERIFY(result.ok());
    QVERIFY(result.expr->evaluate(context));
  }
  {
    ExprParseResult result = parseLibraryExpression(
        base + QStringLiteral(" IN [x, b / c]"), registry);
    QVERIFY(result.ok());
    QVERIFY(result.expr->evaluate(context));
  }

  context.values.insert_or_assign("artist", FieldValue("x", "artist"));
  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" IS b / c"), registry);
    QVERIFY(result.ok());
    QVERIFY(!result.expr->evaluate(context));
  }
  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" HAS b"), registry);
    QVERIFY(result.ok());
    QVERIFY(!result.expr->evaluate(context));
  }
}

void TestLibraryExpression::evaluate_ifExprSupportsAllNumberOperators() {
  ColumnRegistry registry;
  TestEvalContext context;
  context.values.emplace("artist", FieldValue("a", "artist"));

  const QString base = QStringLiteral("(IF artist IS a THEN 10 ELSE 20)");

  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" IS 10"), registry);
    QVERIFY(result.ok());
    QVERIFY(result.expr->evaluate(context));
  }
  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" = 10"), registry);
    QVERIFY(result.ok());
    QVERIFY(result.expr->evaluate(context));
  }
  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" IN [5, 10]"), registry);
    QVERIFY(result.ok());
    QVERIFY(result.expr->evaluate(context));
  }
  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" IN [9..11]"), registry);
    QVERIFY(result.ok());
    QVERIFY(result.expr->evaluate(context));
  }
  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" < 11"), registry);
    QVERIFY(result.ok());
    QVERIFY(result.expr->evaluate(context));
  }
  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" <= 10"), registry);
    QVERIFY(result.ok());
    QVERIFY(result.expr->evaluate(context));
  }
  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" > 9"), registry);
    QVERIFY(result.ok());
    QVERIFY(result.expr->evaluate(context));
  }
  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" >= 10"), registry);
    QVERIFY(result.ok());
    QVERIFY(result.expr->evaluate(context));
  }

  context.values.insert_or_assign("artist", FieldValue("x", "artist"));
  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" < 11"), registry);
    QVERIFY(result.ok());
    QVERIFY(!result.expr->evaluate(context));
  }
  {
    ExprParseResult result =
        parseLibraryExpression(base + QStringLiteral(" >= 20"), registry);
    QVERIFY(result.ok());
    QVERIFY(result.expr->evaluate(context));
  }
}

void TestLibraryExpression::evaluate_nestedIfHasComparedWithIsBool() {
  ColumnRegistry registry;

  ExprParseResult isTrue = parseLibraryExpression(
      QStringLiteral("((IF artist IS a THEN b / c ELSE d / e) HAS b) IS true"),
      registry);
  QVERIFY(isTrue.ok());

  ExprParseResult isFalse = parseLibraryExpression(
      QStringLiteral("((IF artist IS a THEN b / c ELSE d / e) HAS b) IS false"),
      registry);
  QVERIFY(isFalse.ok());

  TestEvalContext context;
  context.values.emplace("artist", FieldValue("a", "artist"));
  QVERIFY(isTrue.expr->evaluate(context));
  QVERIFY(!isFalse.expr->evaluate(context));

  context.values.insert_or_assign("artist", FieldValue("x", "artist"));
  QVERIFY(!isTrue.expr->evaluate(context));
  QVERIFY(isFalse.expr->evaluate(context));
}

void TestLibraryExpression::reject_ifExprComparedWithIsDifferentType() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("(IF artist IS a THEN 1 ELSE 2) IS two"), registry);
  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("not valid"));
}

void TestLibraryExpression::reject_ifWithNonBooleanCondition() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("IF \"x\" THEN \"a\" ELSE \"b\""), registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("condition must be boolean"));
}

void TestLibraryExpression::reject_ifWithMismatchedBranchTypes() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("IF artist IS a THEN 1 ELSE \"b\""), registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("same type"));
}

void TestLibraryExpression::reject_nestedBooleanOpInIfBranch() {
  ColumnRegistry registry;

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("IF artist IS a THEN (\"x\" OR genre IS rock) ELSE \"y\""),
      registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("OR operands must be boolean"));
}

void TestLibraryExpression::reject_unknownField() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("unknown IS value"), registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("Unknown field"));
}

void TestLibraryExpression::reject_internalAttrKeyInExpression() {
  ColumnRegistry registry;
  registry.addOrUpdateDynamicColumn({.id = "attr:musicbrainz_trackid",
                                     .title = "MusicBrainz Track ID",
                                     .sortable = true,
                                     .visibleByDefault = true,
                                     .defaultWidth = 140},
                                    {.id = "attr:musicbrainz_trackid",
                                     .source = ColumnSource::SongAttribute,
                                     .valueType = ValueType::Text,
                                     .displayKind = DisplayKind::Raw,
                                     .expression = "",
                                     .searchable = true,
                                     .writable = true});

  ExprParseResult result = parseLibraryExpression(
      QStringLiteral("attr:musicbrainz_trackid IS abc123"), registry);

  QVERIFY(result.ok());
}

void TestLibraryExpression::reject_runtimeSymbolInDefaultParseContext() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("isplaying IS true"), registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("Unknown field"));
}

void TestLibraryExpression::reject_computedField() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("status IS playing"), registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("Unknown field"));
}

void TestLibraryExpression::reject_missingOperatorOrValue() {
  ColumnRegistry registry;

  ExprParseResult missingOperator =
      parseLibraryExpression(QStringLiteral("artist pop"), registry);
  QVERIFY(missingOperator.ok());
  ExprPtr missingOperatorExpected = makeLiteral("artist pop");
  QVERIFY(*missingOperator.expr == *missingOperatorExpected);

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
  QVERIFY(result.error.message.contains("AND operands must be boolean"));
}

void TestLibraryExpression::reject_andWithNonBooleanOperand() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("\"x\" AND artist IS a"), registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("AND operands must be boolean"));
}

void TestLibraryExpression::reject_orWithNonBooleanOperand() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("artist IS a OR \"x\""), registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("OR operands must be boolean"));
}

void TestLibraryExpression::reject_notWithNonBooleanOperand() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("NOT \"x\""), registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("NOT operand must be boolean"));
}

void TestLibraryExpression::reject_unterminatedQuotedValue() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("title IS \"unfinished"), registry);

  QVERIFY(!result.ok());
  QVERIFY(result.error.message.contains("Unterminated quoted string"));
}

void TestLibraryExpression::reject_unterminatedInterpolationPlaceholder() {
  ColumnRegistry registry;

  ExprParseResult result =
      parseLibraryExpression(QStringLiteral("`${codec`"), registry);

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
