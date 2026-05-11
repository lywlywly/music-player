#include "libraryexpression_parser.h"

#include "columnregistry.h"
#include "libraryexpression_ops.h"
#include "libraryexpression_tokenizer.h"
#include "utils.h"

#include <cctype>
#include <functional>
#include <tuple>
#include <utility>

namespace {
std::string_view trimAsciiWhitespace(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return text.substr(begin, end - begin);
}

bool splitRangeBoundaries(std::string_view raw, std::string &start,
                          std::string &end) {
  const std::size_t sep = raw.find("..");
  if (sep == std::string_view::npos ||
      raw.find("..", sep + 2) != std::string_view::npos) {
    return false;
  }

  const std::string_view left = trimAsciiWhitespace(raw.substr(0, sep));
  const std::string_view right = trimAsciiWhitespace(raw.substr(sep + 2));
  if (left.empty() || right.empty()) {
    return false;
  }

  start.assign(left);
  end.assign(right);
  return true;
}

struct ParseContext {
  const QString &expressionText;
  const ColumnRegistry &registry;
  const std::vector<ExprToken> &tokens;
};

template <typename T> struct ParseStep {
  T value{};
  int nextIndex = 0;
  ExprParseError error;

  bool ok() const { return error.message.isEmpty(); }
};

template <typename T>
ParseStep<T> makeErrorStep(const QString &message, int position) {
  return ParseStep<T>{
      .error = ExprParseError{.message = message, .position = position}};
}

const ExprToken &peek(const std::vector<ExprToken> &tokens, int index) {
  return tokens[index];
}

template <typename T>
using ParserFn = std::function<ParseStep<T>(const ParseContext &, int)>;

template <typename T, typename Transform>
auto map(ParserFn<T> parser, Transform transform)
    -> ParserFn<std::invoke_result_t<Transform, T>> {
  using U = std::invoke_result_t<Transform, T>;
  return [parser = std::move(parser), transform = std::move(transform)](
             const ParseContext &context, int index) mutable -> ParseStep<U> {
    auto step = parser(context, index);
    if (!step.ok()) {
      return makeErrorStep<U>(step.error.message, step.error.position);
    }

    return ParseStep<U>{.value = std::invoke(transform, std::move(step.value)),
                        .nextIndex = step.nextIndex};
  };
}

template <typename First>
ParserFn<std::tuple<First>> sequence(ParserFn<First> first) {
  return [first = std::move(first)](const ParseContext &context,
                                    int index) -> ParseStep<std::tuple<First>> {
    auto step = first(context, index);
    if (!step.ok()) {
      return makeErrorStep<std::tuple<First>>(step.error.message,
                                              step.error.position);
    }

    return ParseStep<std::tuple<First>>{
        .value = std::make_tuple(std::move(step.value)),
        .nextIndex = step.nextIndex};
  };
}

template <typename First, typename Second, typename... Rest>
ParserFn<std::tuple<First, Second, Rest...>> sequence(ParserFn<First> first,
                                                      ParserFn<Second> second,
                                                      ParserFn<Rest>... rest) {
  auto restParser = sequence(std::move(second), std::move(rest)...);
  return
      [first = std::move(first), restParser = std::move(restParser)](
          const ParseContext &context,
          int index) mutable -> ParseStep<std::tuple<First, Second, Rest...>> {
        auto left = first(context, index);
        if (!left.ok()) {
          return makeErrorStep<std::tuple<First, Second, Rest...>>(
              left.error.message, left.error.position);
        }

        auto right = restParser(context, left.nextIndex);
        if (!right.ok()) {
          return makeErrorStep<std::tuple<First, Second, Rest...>>(
              right.error.message, right.error.position);
        }

        return ParseStep<std::tuple<First, Second, Rest...>>{
            .value = std::tuple_cat(std::make_tuple(std::move(left.value)),
                                    std::move(right.value)),
            .nextIndex = right.nextIndex};
      };
}

template <typename Combine>
ParserFn<ExprPtr> chainLeft(ParserFn<ExprPtr> operand,
                            ExprTokenKind operatorKind, Combine combine,
                            ExprStaticType expectedType, QString operatorName) {
  return
      [operand = std::move(operand), operatorKind, combine = std::move(combine),
       expectedType, operatorName = std::move(operatorName)](
          const ParseContext &context,
          int index) mutable -> ParseStep<ExprPtr> {
        auto left = operand(context, index);
        if (!left.ok()) {
          return left;
        }

        while (peek(context.tokens, left.nextIndex).kind == operatorKind) {
          const int operatorIndex = left.nextIndex;
          auto right = operand(context, operatorIndex + 1);
          if (!right.ok()) {
            return right;
          }

          if (inferExprStaticType(*left.value) != expectedType ||
              inferExprStaticType(*right.value) != expectedType) {
            return makeErrorStep<ExprPtr>(
                QStringLiteral("%1 operands must be boolean").arg(operatorName),
                peek(context.tokens, operatorIndex).start);
          }

          left.value = combine(std::move(left.value), std::move(right.value));
          left.nextIndex = right.nextIndex;
        }
        return left;
      };
}

ParseStep<ExprPtr> parseOr(const ParseContext &context, int index);
ParseStep<ExprPtr> parseAnd(const ParseContext &context, int index);
ParseStep<ExprPtr> parseUnary(const ParseContext &context, int index);
ParseStep<ExprPtr> parsePrimary(const ParseContext &context, int index);
ParseStep<ExprPtr> parseGroupedExpr(const ParseContext &context, int index);
ParseStep<ExprPtr> parseIfExpr(const ParseContext &context, int index);
ParseStep<ExprPtr> parseLiteralExpr(const ParseContext &context, int index);
ParseStep<ExprPtr> parseComparisonExpr(const ParseContext &context, int index);
ParseStep<ExprPtr> parseExprValueComparisonSuffix(const ParseContext &context,
                                                  ParseStep<ExprPtr> left);
ParseStep<ExprFieldRef> parseFieldRef(const ParseContext &context, int index);
ParseStep<ExprFieldRef> resolveFieldRefTokenText(const ParseContext &context,
                                                 const QString &fieldText,
                                                 int position);
ParseStep<ExprOperatorPtr> parseComparisonOperator(const ParseContext &context,
                                                   int index);
ParseStep<ExprValue> parseValue(const ParseContext &context, int index);
ParseStep<ExprValue> parseListValue(const ParseContext &context, int index);
ParseStep<ExprPtr> parseInterpolatedStringLiteral(const ParseContext &context,
                                                  int index);

ExprRuntimeValue parseLiteralRuntimeValue(const QString &rawText) {
  if (rawText.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
    return ExprRuntimeValue::fromBool(true);
  }
  if (rawText.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
    return ExprRuntimeValue::fromBool(false);
  }

  bool numberOk = false;
  const double numberValue = rawText.toDouble(&numberOk);
  if (numberOk) {
    return ExprRuntimeValue::fromNumber(numberValue);
  }

  return ExprRuntimeValue::fromText(rawText.toStdString());
}

ColumnValueType columnValueTypeFromExprType(ExprStaticType exprType) {
  switch (exprType) {
  case ExprStaticType::Bool:
    return ColumnValueType::Boolean;
  case ExprStaticType::Number:
    return ColumnValueType::Number;
  case ExprStaticType::Text:
  default:
    return ColumnValueType::Text;
  }
}

bool isComparisonOperatorToken(ExprTokenKind kind) {
  return kind == ExprTokenKind::KeywordIs || kind == ExprTokenKind::OpEq ||
         kind == ExprTokenKind::KeywordHas ||
         kind == ExprTokenKind::KeywordIn || kind == ExprTokenKind::OpLt ||
         kind == ExprTokenKind::OpLte || kind == ExprTokenKind::OpGt ||
         kind == ExprTokenKind::OpGte;
}

ParseStep<ExprPtr> parseOr(const ParseContext &context, int index) {
  static const auto parser = chainLeft(
      ParserFn<ExprPtr>(parseAnd), ExprTokenKind::KeywordOr,
      [](ExprPtr left, ExprPtr right) {
        return std::make_unique<OrExpr>(std::move(left), std::move(right));
      },
      ExprStaticType::Bool, QStringLiteral("OR"));
  return parser(context, index);
}

ParseStep<ExprPtr> parseAnd(const ParseContext &context, int index) {
  static const auto parser = chainLeft(
      ParserFn<ExprPtr>(parseUnary), ExprTokenKind::KeywordAnd,
      [](ExprPtr left, ExprPtr right) {
        return std::make_unique<AndExpr>(std::move(left), std::move(right));
      },
      ExprStaticType::Bool, QStringLiteral("AND"));
  return parser(context, index);
}

ParseStep<ExprPtr> parseUnary(const ParseContext &context, int index) {
  const ExprToken &token = peek(context.tokens, index);
  if (token.kind == ExprTokenKind::KeywordNot) {
    auto child = parseUnary(context, index + 1);
    if (!child.ok()) {
      return child;
    }
    if (inferExprStaticType(*child.value) != ExprStaticType::Bool) {
      return makeErrorStep<ExprPtr>(
          QStringLiteral("NOT operand must be boolean"), token.start);
    }

    return ParseStep<ExprPtr>{
        .value = std::make_unique<NotExpr>(std::move(child.value)),
        .nextIndex = child.nextIndex};
  }
  return parsePrimary(context, index);
}

ParseStep<ExprPtr> parsePrimary(const ParseContext &context, int index) {
  const ExprToken &token = peek(context.tokens, index);
  if (token.kind == ExprTokenKind::LParen) {
    return parseExprValueComparisonSuffix(context,
                                          parseGroupedExpr(context, index));
  }
  if (token.kind == ExprTokenKind::KeywordIf) {
    return parseExprValueComparisonSuffix(context, parseIfExpr(context, index));
  }
  if (token.kind == ExprTokenKind::StringLiteral) {
    return parseExprValueComparisonSuffix(context,
                                          parseLiteralExpr(context, index));
  }
  if (token.kind == ExprTokenKind::Identifier &&
      !isComparisonOperatorToken(peek(context.tokens, index + 1).kind)) {
    return parseExprValueComparisonSuffix(context,
                                          parseLiteralExpr(context, index));
  }
  return parseComparisonExpr(context, index);
}

ParseStep<ExprPtr> parseGroupedExpr(const ParseContext &context, int index) {
  auto expr = parseOr(context, index + 1);
  if (!expr.ok()) {
    return expr;
  }

  const ExprToken &close = peek(context.tokens, expr.nextIndex);
  if (close.kind != ExprTokenKind::RParen) {
    return makeErrorStep<ExprPtr>(QStringLiteral("Expected `)`"), close.start);
  }

  expr.nextIndex += 1;
  return expr;
}

ParseStep<ExprPtr> parseIfExpr(const ParseContext &context, int index) {
  static const auto parseThen = [](const ParseContext &ctx,
                                   int i) -> ParseStep<ExprTokenKind> {
    const ExprToken &token = peek(ctx.tokens, i);
    if (token.kind != ExprTokenKind::KeywordThen) {
      return makeErrorStep<ExprTokenKind>(QStringLiteral("Expected `THEN`"),
                                          token.start);
    }
    return ParseStep<ExprTokenKind>{.value = token.kind, .nextIndex = i + 1};
  };

  static const auto parseElse = [](const ParseContext &ctx,
                                   int i) -> ParseStep<ExprTokenKind> {
    const ExprToken &token = peek(ctx.tokens, i);
    if (token.kind != ExprTokenKind::KeywordElse) {
      return makeErrorStep<ExprTokenKind>(QStringLiteral("Expected `ELSE`"),
                                          token.start);
    }
    return ParseStep<ExprTokenKind>{.value = token.kind, .nextIndex = i + 1};
  };

  static const auto parser = map(
      sequence(ParserFn<ExprPtr>(parseOr), ParserFn<ExprTokenKind>(parseThen),
               ParserFn<ExprPtr>(parseOr), ParserFn<ExprTokenKind>(parseElse),
               ParserFn<ExprPtr>(parseOr)),
      [](std::tuple<ExprPtr, ExprTokenKind, ExprPtr, ExprTokenKind, ExprPtr>
             parsed) -> ExprPtr {
        auto condition = std::move(std::get<0>(parsed));
        auto thenExpr = std::move(std::get<2>(parsed));
        auto elseExpr = std::move(std::get<4>(parsed));
        return std::make_unique<IfExpr>(
            std::move(condition), std::move(thenExpr), std::move(elseExpr));
      });

  ParseStep<ExprPtr> step = parser(context, index + 1);
  if (!step.ok()) {
    return step;
  }

  const auto *ifExpr = static_cast<const IfExpr *>(step.value.get());
  if (inferExprStaticType(*ifExpr->condition) != ExprStaticType::Bool) {
    return makeErrorStep<ExprPtr>(
        QStringLiteral("IF condition must be boolean"),
        peek(context.tokens, index).start);
  }

  const ExprStaticType thenType = inferExprStaticType(*ifExpr->thenExpr);
  const ExprStaticType elseType = inferExprStaticType(*ifExpr->elseExpr);
  if (thenType == ExprStaticType::Invalid ||
      elseType == ExprStaticType::Invalid || thenType != elseType) {
    return makeErrorStep<ExprPtr>(
        QStringLiteral("THEN and ELSE must return the same type"),
        peek(context.tokens, index).start);
  }

  return step;
}

ParseStep<ExprPtr> parseLiteralExpr(const ParseContext &context, int index) {
  const ExprToken &token = peek(context.tokens, index);
  if (token.kind == ExprTokenKind::StringLiteral) {
    return parseInterpolatedStringLiteral(context, index);
  }
  if (token.kind != ExprTokenKind::Identifier) {
    return makeErrorStep<ExprPtr>(
        QStringLiteral("Expected a literal expression"), token.start);
  }

  const int start = token.start;
  int end = token.end;
  index += 1;
  while (peek(context.tokens, index).kind == ExprTokenKind::Identifier) {
    end = peek(context.tokens, index).end;
    index += 1;
  }

  const QString text = context.expressionText.mid(start, end - start).trimmed();
  return ParseStep<ExprPtr>{
      .value = std::make_unique<LiteralExpr>(parseLiteralRuntimeValue(text)),
      .nextIndex = index};
}

ParseStep<ExprPtr> parseInterpolatedStringLiteral(const ParseContext &context,
                                                  int index) {
  const ExprToken &token = peek(context.tokens, index);
  const QString content = token.text;
  if (!content.contains(QStringLiteral("${"))) {
    return ParseStep<ExprPtr>{
        .value = std::make_unique<LiteralExpr>(
            ExprRuntimeValue::fromText(token.text.toStdString())),
        .nextIndex = index + 1};
  }
  std::vector<InterpolatedStringPart> parts;
  const auto absolutePos = [&](int contentOffset) {
    return token.start + 1 + contentOffset;
  };
  const auto appendTextPart = [&](const QString &text) {
    if (text.isEmpty()) {
      return;
    }
    parts.push_back(
        InterpolatedStringPart{.text = text.toStdString(), .expr = nullptr});
  };
  int cursor = 0;

  while (cursor < content.size()) {
    const int marker = content.indexOf(QStringLiteral("${"), cursor);
    if (marker < 0) {
      appendTextPart(content.mid(cursor));
      break;
    }

    if (marker > cursor) {
      appendTextPart(content.mid(cursor, marker - cursor));
    }

    const int exprStart = marker + 2;
    const int exprEnd = content.indexOf(QChar('}'), exprStart);
    if (exprEnd < 0) {
      return makeErrorStep<ExprPtr>(
          QStringLiteral("Unterminated interpolation `${...}`"),
          absolutePos(marker));
    }

    const QString innerExprText = content.mid(exprStart, exprEnd - exprStart);
    if (innerExprText.trimmed().isEmpty()) {
      return makeErrorStep<ExprPtr>(QStringLiteral("Empty interpolation `${}`"),
                                    absolutePos(exprStart));
    }

    ExprPtr innerExpr;
    const std::vector<ExprToken> innerTokens =
        tokenizeLibraryExpression(innerExprText);
    if (innerTokens.size() == 2 &&
        innerTokens.front().kind == ExprTokenKind::Identifier &&
        innerTokens.back().kind == ExprTokenKind::End) {
      auto resolved = resolveFieldRefTokenText(
          context, innerTokens.front().text, absolutePos(exprStart));
      if (resolved.ok()) {
        innerExpr = std::make_unique<FieldRefExpr>(std::move(resolved.value));
      }
    }

    if (!innerExpr) {
      ExprParseResult embedded =
          parseLibraryExpression(innerExprText, context.registry);
      if (!embedded.ok()) {
        const int relativeErrorPos =
            embedded.error.position < 0 ? 0 : embedded.error.position;
        return makeErrorStep<ExprPtr>(
            embedded.error.message, absolutePos(exprStart + relativeErrorPos));
      }
      innerExpr = std::move(embedded.expr);
    }

    parts.push_back(InterpolatedStringPart{.text = std::string{},
                                           .expr = std::move(innerExpr)});
    cursor = exprEnd + 1;
  }

  return ParseStep<ExprPtr>{
      .value = std::make_unique<InterpolatedStringExpr>(std::move(parts)),
      .nextIndex = index + 1};
}

ParseStep<ExprPtr> parseExprValueComparisonSuffix(const ParseContext &context,
                                                  ParseStep<ExprPtr> left) {
  if (!left.ok()) {
    return left;
  }

  const int suffixIndex = left.nextIndex;
  const ExprToken &operatorToken = peek(context.tokens, suffixIndex);
  if (!isComparisonOperatorToken(operatorToken.kind)) {
    return left;
  }

  static const auto parseOpValue =
      sequence(ParserFn<ExprOperatorPtr>(parseComparisonOperator),
               ParserFn<ExprValue>(parseValue));

  ParseStep<std::tuple<ExprOperatorPtr, ExprValue>> opValueStep =
      parseOpValue(context, suffixIndex);
  if (!opValueStep.ok()) {
    return makeErrorStep<ExprPtr>(opValueStep.error.message,
                                  opValueStep.error.position);
  }
  const ExprOperatorPtr &op = std::get<0>(opValueStep.value);
  const ExprValue &rightValue = std::get<1>(opValueStep.value);

  const ExprStaticType leftType = inferExprStaticType(*left.value);
  if (leftType == ExprStaticType::Invalid) {
    return makeErrorStep<ExprPtr>(QStringLiteral("Invalid expression type"),
                                  operatorToken.start);
  }
  const ColumnValueType leftValueType = columnValueTypeFromExprType(leftType);

  if (!op->supportsValue(rightValue)) {
    return makeErrorStep<ExprPtr>(
        QStringLiteral("Operator `%1` does not support this value form")
            .arg(QString::fromStdString(op->displayName())),
        operatorToken.start);
  }
  if (!exprValueMatchesFieldType(rightValue, leftValueType)) {
    return makeErrorStep<ExprPtr>(
        QStringLiteral("Value is not valid for this expression type"),
        operatorToken.start);
  }
  if (rightValue.isRange() && !supportsRangeValueType(leftValueType)) {
    return makeErrorStep<ExprPtr>(
        QStringLiteral("Range values are only supported for numeric and "
                       "datetime fields"),
        operatorToken.start);
  }
  if (!isRangeBoundaryOrderValid(rightValue, leftValueType)) {
    return makeErrorStep<ExprPtr>(
        QStringLiteral("Range start must be less than or equal to range end"),
        operatorToken.start);
  }

  return ParseStep<ExprPtr>{.value = std::make_unique<ComparisonExpr>(
                                std::move(left.value),
                                std::move(std::get<0>(opValueStep.value)),
                                std::move(std::get<1>(opValueStep.value))),
                            .nextIndex = opValueStep.nextIndex};
}

ParseStep<ExprPtr> parseComparisonExpr(const ParseContext &context, int index) {
  static const auto parser =
      map(sequence(ParserFn<ExprFieldRef>(parseFieldRef),
                   ParserFn<ExprOperatorPtr>(parseComparisonOperator),
                   ParserFn<ExprValue>(parseValue)),
          [](std::tuple<ExprFieldRef, ExprOperatorPtr, ExprValue> parsed)
              -> std::unique_ptr<Expr> {
            auto [field, op, value] = std::move(parsed);
            return std::make_unique<ComparisonExpr>(
                std::move(field), std::move(op), std::move(value));
          });
  ParseStep<ExprPtr> step = parser(context, index);
  if (!step.ok()) {
    return step;
  }

  ComparisonExpr *comparison = static_cast<ComparisonExpr *>(step.value.get());
  if (!comparison->op->supportsValue(comparison->value)) {
    return makeErrorStep<ExprPtr>(
        QStringLiteral("Operator `%1` does not support this value form")
            .arg(QString::fromStdString(comparison->op->displayName())),
        peek(context.tokens, index).start);
  }
  if (!exprValueMatchesFieldType(comparison->value,
                                 comparison->field.valueType)) {
    return makeErrorStep<ExprPtr>(
        QStringLiteral("Value is not valid for field `%1`")
            .arg(QString::fromStdString(comparison->field.exprFieldName)),
        peek(context.tokens, index).start);
  }
  if (comparison->value.isRange() &&
      !supportsRangeValueType(comparison->field.valueType)) {
    return makeErrorStep<ExprPtr>(
        QStringLiteral("Range values are only supported for numeric and "
                       "datetime fields"),
        peek(context.tokens, index).start);
  }
  if (!isRangeBoundaryOrderValid(comparison->value,
                                 comparison->field.valueType)) {
    return makeErrorStep<ExprPtr>(
        QStringLiteral("Range start must be less than or equal to range end"),
        peek(context.tokens, index).start);
  }

  return step;
}

ParseStep<ExprFieldRef> parseFieldRef(const ParseContext &context, int index) {
  const ExprToken &fieldToken = peek(context.tokens, index);
  if (fieldToken.kind == ExprTokenKind::Invalid) {
    return makeErrorStep<ExprFieldRef>(
        QStringLiteral("Unterminated quoted string"), fieldToken.start);
  }
  if (fieldToken.kind != ExprTokenKind::Identifier) {
    return makeErrorStep<ExprFieldRef>(QStringLiteral("Expected a field name"),
                                       fieldToken.start);
  }

  auto resolved =
      resolveFieldRefTokenText(context, fieldToken.text, fieldToken.start);
  if (!resolved.ok()) {
    return resolved;
  }
  resolved.nextIndex = index + 1;
  return resolved;
}

ParseStep<ExprFieldRef> resolveFieldRefTokenText(const ParseContext &context,
                                                 const QString &fieldText,
                                                 int position) {
  const QString normalizedFieldQ = util::normalizedText(fieldText);
  const std::string normalizedField = normalizedFieldQ.toStdString();

  const ColumnDefinition *direct =
      context.registry.findColumn(normalizedFieldQ);
  if (direct) {
    const bool searchableDirect =
        direct->source == ColumnSource::SongAttribute ||
        (direct->source == ColumnSource::Computed &&
         !direct->expression.trimmed().isEmpty());
    if (!searchableDirect) {
      return makeErrorStep<ExprFieldRef>(
          QStringLiteral("Field `%1` is not searchable").arg(fieldText),
          position);
    }

    return ParseStep<ExprFieldRef>{
        .value =
            ExprFieldRef{normalizedField, normalizedField, direct->valueType}};
  }

  const QString customColumnIdQ = QStringLiteral("attr:") + normalizedFieldQ;
  const std::string customColumnId = customColumnIdQ.toStdString();
  const ColumnDefinition *custom = context.registry.findColumn(customColumnIdQ);
  if (!custom || custom->source != ColumnSource::SongAttribute) {
    return makeErrorStep<ExprFieldRef>(
        QStringLiteral("Unknown field `%1`").arg(fieldText), position);
  }

  return ParseStep<ExprFieldRef>{
      .value =
          ExprFieldRef{normalizedField, customColumnId, custom->valueType}};
}

ParseStep<ExprOperatorPtr> parseComparisonOperator(const ParseContext &context,
                                                   int index) {
  const ExprToken &opToken = peek(context.tokens, index);
  if (opToken.kind == ExprTokenKind::KeywordIs ||
      opToken.kind == ExprTokenKind::OpEq) {
    return ParseStep<ExprOperatorPtr>{.value = std::make_unique<IsOperator>(),
                                      .nextIndex = index + 1};
  }
  if (opToken.kind == ExprTokenKind::KeywordHas) {
    return ParseStep<ExprOperatorPtr>{.value = std::make_unique<HasOperator>(),
                                      .nextIndex = index + 1};
  }
  if (opToken.kind == ExprTokenKind::KeywordIn) {
    return ParseStep<ExprOperatorPtr>{.value = std::make_unique<InOperator>(),
                                      .nextIndex = index + 1};
  }
  if (opToken.kind == ExprTokenKind::OpLt) {
    return ParseStep<ExprOperatorPtr>{.value = std::make_unique<LtOperator>(),
                                      .nextIndex = index + 1};
  }
  if (opToken.kind == ExprTokenKind::OpLte) {
    return ParseStep<ExprOperatorPtr>{.value = std::make_unique<LteOperator>(),
                                      .nextIndex = index + 1};
  }
  if (opToken.kind == ExprTokenKind::OpGt) {
    return ParseStep<ExprOperatorPtr>{.value = std::make_unique<GtOperator>(),
                                      .nextIndex = index + 1};
  }
  if (opToken.kind == ExprTokenKind::OpGte) {
    return ParseStep<ExprOperatorPtr>{.value = std::make_unique<GteOperator>(),
                                      .nextIndex = index + 1};
  }
  return makeErrorStep<ExprOperatorPtr>(
      QStringLiteral("Expected comparison operator"), opToken.start);
}

ParseStep<ExprValue> parseValue(const ParseContext &context, int index) {
  const ExprToken &token = peek(context.tokens, index);
  if (token.kind == ExprTokenKind::LBracket) {
    return parseListValue(context, index);
  }
  if (token.kind == ExprTokenKind::Invalid) {
    return makeErrorStep<ExprValue>(
        QStringLiteral("Unterminated quoted string"), token.start);
  }
  if (token.kind == ExprTokenKind::End) {
    return makeErrorStep<ExprValue>(
        QStringLiteral("Expected a value after comparison operator"),
        token.start);
  }
  if (token.kind != ExprTokenKind::Identifier &&
      token.kind != ExprTokenKind::StringLiteral) {
    return makeErrorStep<ExprValue>(
        QStringLiteral("Unexpected token `%1` while expecting a value")
            .arg(token.text),
        token.start);
  }

  if (token.kind == ExprTokenKind::StringLiteral) {
    return ParseStep<ExprValue>{
        .value = ExprValue{.kind = ExprValue::Kind::Scalar,
                           .values = {token.text.toStdString()}},
        .nextIndex = index + 1};
  }

  const int start = token.start;
  int end = token.end;
  index += 1;

  while (peek(context.tokens, index).kind == ExprTokenKind::Identifier) {
    end = peek(context.tokens, index).end;
    index += 1;
  }

  const QString rawValue =
      context.expressionText.mid(start, end - start).trimmed();
  if (rawValue.isEmpty()) {
    return makeErrorStep<ExprValue>(
        QStringLiteral("Expected a value after comparison operator"), start);
  }

  return ParseStep<ExprValue>{.value =
                                  ExprValue{.kind = ExprValue::Kind::Scalar,
                                            .values = {rawValue.toStdString()}},
                              .nextIndex = index};
}

ParseStep<ExprValue> parseListValue(const ParseContext &context, int index) {
  ExprValue value;
  bool sawQuotedItem = false;
  index += 1;

  while (true) {
    const ExprToken &token = peek(context.tokens, index);
    if (token.kind == ExprTokenKind::Invalid) {
      return makeErrorStep<ExprValue>(
          QStringLiteral("Unterminated quoted string"), token.start);
    }
    if (token.kind == ExprTokenKind::RBracket) {
      if (value.values.empty()) {
        return makeErrorStep<ExprValue>(
            QStringLiteral("Expected at least one value in list"), token.start);
      }

      if (value.values.size() == 1 && !sawQuotedItem) {
        std::string rangeStart;
        std::string rangeEnd;
        if (splitRangeBoundaries(value.values.front(), rangeStart, rangeEnd)) {
          value.kind = ExprValue::Kind::Range;
          value.values = {std::move(rangeStart), std::move(rangeEnd)};
        } else {
          value.kind = ExprValue::Kind::List;
        }
      } else {
        value.kind = ExprValue::Kind::List;
      }

      return ParseStep<ExprValue>{.value = std::move(value),
                                  .nextIndex = index + 1};
    }

    if (token.kind != ExprTokenKind::Identifier &&
        token.kind != ExprTokenKind::StringLiteral) {
      return makeErrorStep<ExprValue>(QStringLiteral("Expected a list value"),
                                      token.start);
    }

    if (token.kind == ExprTokenKind::StringLiteral) {
      sawQuotedItem = true;
      value.values.push_back(token.text.toStdString());
      index += 1;
    } else {
      const int start = token.start;
      int end = token.end;
      index += 1;
      while (peek(context.tokens, index).kind == ExprTokenKind::Identifier) {
        end = peek(context.tokens, index).end;
        index += 1;
      }

      const QString rawValue =
          context.expressionText.mid(start, end - start).trimmed();
      if (rawValue.isEmpty()) {
        return makeErrorStep<ExprValue>(QStringLiteral("Expected a list value"),
                                        start);
      }
      value.values.push_back(rawValue.toStdString());
    }

    const ExprToken &separator = peek(context.tokens, index);
    if (separator.kind == ExprTokenKind::Comma) {
      index += 1;
      continue;
    }
    if (separator.kind == ExprTokenKind::RBracket) {
      continue;
    }
    return makeErrorStep<ExprValue>(
        QStringLiteral("Expected `,` or `]` in list"), separator.start);
  }
}
} // namespace

ExprParseResult parseLibraryExpression(const QString &expressionText,
                                       const ColumnRegistry &registry) {
  if (expressionText.trimmed().isEmpty()) {
    return ExprParseResult{
        .error = ExprParseError{.message = QStringLiteral("Expression cannot "
                                                          "be empty"),
                                .position = 0}};
  }

  const std::vector<ExprToken> tokens =
      tokenizeLibraryExpression(expressionText);
  const ParseContext context{expressionText, registry, tokens};
  auto parsed = parseOr(context, 0);
  if (!parsed.ok()) {
    return ExprParseResult{.error = parsed.error};
  }

  const ExprToken &nextToken = peek(tokens, parsed.nextIndex);
  if (nextToken.kind != ExprTokenKind::End) {
    return ExprParseResult{
        .error = ExprParseError{
            .message =
                QStringLiteral("Unexpected token `%1`").arg(nextToken.text),
            .position = nextToken.start}};
  }

  return ExprParseResult{.expr = std::move(parsed.value)};
}
