#include "libraryexpression_parser.h"

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

int findInterpolationEnd(const QString &text, int expressionStart) {
  int depth = 1;
  int i = expressionStart;
  while (i < text.size()) {
    const QChar ch = text.at(i);
    if (ch == '"' || ch == '\'' || ch == '`') {
      const QChar quote = ch;
      ++i;
      while (i < text.size() && text.at(i) != quote) {
        ++i;
      }
      if (i >= text.size()) {
        return -1;
      }
      ++i;
      continue;
    }
    if (ch == '$' && i + 1 < text.size() && text.at(i + 1) == '{') {
      depth += 1;
      i += 2;
      continue;
    }
    if (ch == '}') {
      depth -= 1;
      if (depth == 0) {
        return i;
      }
      ++i;
      continue;
    }
    ++i;
  }
  return -1;
}

bool containsUnquotedInterpolationMarker(const QString &text) {
  int i = 0;
  while (i < text.size()) {
    const QChar ch = text.at(i);
    if (ch == '"' || ch == '\'' || ch == '`') {
      const QChar quote = ch;
      ++i;
      while (i < text.size() && text.at(i) != quote) {
        ++i;
      }
      if (i >= text.size()) {
        return false;
      }
      ++i;
      continue;
    }
    if (ch == '$' && i + 1 < text.size() && text.at(i + 1) == '{') {
      return true;
    }
    ++i;
  }
  return false;
}

struct ParseContext {
  const QString &expressionText;
  const std::vector<ExprToken> &tokens;
  const ExprSymbolResolver &resolver;
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

// Required forward declarations for recursive parse flow and out-of-order uses.
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
ParseStep<ExprPtr> parseComparisonTail(const ParseContext &context,
                                       ExprPtr leftExpr,
                                       ValueType leftValueType,
                                       int operatorIndex, int errorPosition,
                                       const QString &invalidValueMessage);

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

ValueType columnValueTypeFromExprType(ExprStaticType exprType) {
  switch (exprType) {
  case ExprStaticType::Bool:
    return ValueType::Boolean;
  case ExprStaticType::Number:
    return ValueType::Number;
  case ExprStaticType::Text:
  default:
    return ValueType::Text;
  }
}

bool isComparisonOperatorToken(ExprTokenKind kind) {
  return kind == ExprTokenKind::KeywordIs || kind == ExprTokenKind::OpEq ||
         kind == ExprTokenKind::KeywordHas ||
         kind == ExprTokenKind::KeywordIn || kind == ExprTokenKind::OpLt ||
         kind == ExprTokenKind::OpLte || kind == ExprTokenKind::OpGt ||
         kind == ExprTokenKind::OpGte;
}

// Parses one atomic expression that can stand alone before boolean chaining.
ParseStep<ExprPtr> parseExprAtom(const ParseContext &context, int index) {
  const ExprToken &token = peek(context.tokens, index);
  if (token.kind == ExprTokenKind::LParen) {
    return parseGroupedExpr(context, index);
  }
  if (token.kind == ExprTokenKind::KeywordIf) {
    return parseIfExpr(context, index);
  }
  if (token.kind == ExprTokenKind::StringLiteral ||
      token.kind == ExprTokenKind::InterpolatedStringLiteral) {
    return parseLiteralExpr(context, index);
  }
  if (token.kind == ExprTokenKind::Identifier) {
    const ExprTokenKind nextKind = peek(context.tokens, index + 1).kind;
    if (isComparisonOperatorToken(nextKind)) {
      auto resolved =
          resolveFieldRefTokenText(context, token.text, token.start);
      if (resolved.ok()) {
        return ParseStep<ExprPtr>{
            .value = std::make_unique<FieldRefExpr>(std::move(resolved.value)),
            .nextIndex = index + 1};
      }
      return makeErrorStep<ExprPtr>(resolved.error.message,
                                    resolved.error.position);
    }
    return parseLiteralExpr(context, index);
  }
  return makeErrorStep<ExprPtr>(QStringLiteral("Expected an expression"),
                                token.start);
}

// Parses and validates the right side of a comparison. Prefers literal/list
// values, then falls back to expression atoms for expr-vs-expr comparisons.
ParseStep<ExprPtr>
parseComparisonRightExpr(const ParseContext &context, int index,
                         const ExprOperator &op, ValueType leftValueType,
                         int errorPosition,
                         const QString &invalidValueMessage) {
  auto value = parseValue(context, index);
  if (value.ok()) {
    const ExprValue &rightValue = value.value;
    if (!op.supportsValue(rightValue)) {
      return makeErrorStep<ExprPtr>(
          QStringLiteral("Operator `%1` does not support this value form")
              .arg(QString::fromStdString(op.displayName())),
          errorPosition);
    }
    if (!exprValueMatchesFieldType(rightValue, leftValueType)) {
      return makeErrorStep<ExprPtr>(invalidValueMessage, errorPosition);
    }
    if (rightValue.isRange() && !supportsRangeValueType(leftValueType)) {
      return makeErrorStep<ExprPtr>(
          QStringLiteral("Range values are only supported for numeric and "
                         "datetime fields"),
          errorPosition);
    }
    if (!isRangeBoundaryOrderValid(rightValue, leftValueType)) {
      return makeErrorStep<ExprPtr>(
          QStringLiteral("Range start must be less than or equal to range end"),
          errorPosition);
    }
    return ParseStep<ExprPtr>{
        .value = std::make_unique<ExprValueExpr>(std::move(value.value)),
        .nextIndex = value.nextIndex};
  }

  const ExprTokenKind kind = peek(context.tokens, index).kind;
  if (kind == ExprTokenKind::Invalid || kind == ExprTokenKind::End) {
    return makeErrorStep<ExprPtr>(value.error.message, value.error.position);
  }
  auto rightExpr = parseExprAtom(context, index);
  if (!rightExpr.ok()) {
    return rightExpr;
  }

  const ExprStaticType rightType = inferExprStaticType(*rightExpr.value);
  if (rightType == ExprStaticType::Invalid ||
      columnValueTypeFromExprType(rightType) != leftValueType) {
    return makeErrorStep<ExprPtr>(
        QStringLiteral("Comparison expressions must have the same type"),
        errorPosition);
  }

  ExprValue scalarPlaceholder;
  scalarPlaceholder.kind = ExprValue::Kind::Scalar;
  scalarPlaceholder.values = {std::string{}};
  if (!op.supportsValue(scalarPlaceholder)) {
    return makeErrorStep<ExprPtr>(
        QStringLiteral("Operator `%1` requires literal/list/range value")
            .arg(QString::fromStdString(op.displayName())),
        errorPosition);
  }

  return rightExpr;
}

// Entry point of expression parsing. Parses left-associative OR chains and
// delegates each operand to parseAnd() (higher precedence).
ParseStep<ExprPtr> parseOr(const ParseContext &context, int index) {
  static const auto parser = chainLeft(
      ParserFn<ExprPtr>(parseAnd), ExprTokenKind::KeywordOr,
      [](ExprPtr left, ExprPtr right) {
        return std::make_unique<OrExpr>(std::move(left), std::move(right));
      },
      ExprStaticType::Bool, QStringLiteral("OR"));
  return parser(context, index);
}

// Parses left-associative AND chains inside an OR operand, with each operand
// parsed by parseUnary() (next higher-precedence stage).
ParseStep<ExprPtr> parseAnd(const ParseContext &context, int index) {
  static const auto parser = chainLeft(
      ParserFn<ExprPtr>(parseUnary), ExprTokenKind::KeywordAnd,
      [](ExprPtr left, ExprPtr right) {
        return std::make_unique<AndExpr>(std::move(left), std::move(right));
      },
      ExprStaticType::Bool, QStringLiteral("AND"));
  return parser(context, index);
}

// Parses unary NOT, then falls through to primary expressions.
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

// Parses one primary unit as an atom plus optional comparison suffix.
// Generic atom-start failures fall back to field-led comparison diagnostics.
ParseStep<ExprPtr> parsePrimary(const ParseContext &context, int index) {
  auto atom = parseExprAtom(context, index);
  if (atom.ok()) {
    return parseExprValueComparisonSuffix(context, std::move(atom));
  }
  if (atom.error.message != QStringLiteral("Expected an expression")) {
    return atom;
  }
  return parseComparisonExpr(context, index);
}

// Parses parenthesized subexpressions and returns the inner parse result.
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

// Parses `IF <cond> THEN <expr> ELSE <expr>` and validates boolean condition
// plus matching THEN/ELSE static types.
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

// Parses literal expressions:
// - backtick interpolated strings,
// - plain quoted strings,
// - identifier text treated as runtime literal.
ParseStep<ExprPtr> parseLiteralExpr(const ParseContext &context, int index) {
  const ExprToken &token = peek(context.tokens, index);
  if (token.kind == ExprTokenKind::InterpolatedStringLiteral) {
    return parseInterpolatedStringLiteral(context, index);
  }
  if (token.kind == ExprTokenKind::StringLiteral) {
    if (token.text.contains(QStringLiteral("${"))) {
      return makeErrorStep<ExprPtr>(
          QStringLiteral("Interpolation is only supported in backtick strings"),
          token.start);
    }
    return ParseStep<ExprPtr>{
        .value = std::make_unique<LiteralExpr>(
            ExprRuntimeValue::fromText(token.text.toStdString())),
        .nextIndex = index + 1};
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

// Parses backtick string interpolation by splitting literal text and `${...}`
// embedded expressions into a single InterpolatedStringExpr.
ParseStep<ExprPtr> parseInterpolatedStringLiteral(const ParseContext &context,
                                                  int index) {
  const ExprToken &token = peek(context.tokens, index);
  const QString content = token.text;
  std::vector<ExprPtr> parts;
  const auto absolutePos = [&](int contentOffset) {
    return token.start + 1 + contentOffset;
  };
  const auto appendTextPart = [&](const QString &text) {
    if (text.isEmpty()) {
      return;
    }
    parts.push_back(std::make_unique<LiteralExpr>(
        ExprRuntimeValue::fromText(text.toStdString())));
  };
  const auto parseEmbeddedExpr = [&](const QString &innerExprText,
                                     int exprStart) -> ParseStep<ExprPtr> {
    if (innerExprText.trimmed().isEmpty()) {
      return makeErrorStep<ExprPtr>(QStringLiteral("Empty interpolation `${}`"),
                                    absolutePos(exprStart));
    }
    if (containsUnquotedInterpolationMarker(innerExprText)) {
      return makeErrorStep<ExprPtr>(
          QStringLiteral("Nested interpolation must be quoted"),
          absolutePos(exprStart));
    }

    const std::vector<ExprToken> innerTokens =
        tokenizeLibraryExpression(innerExprText);
    if (innerTokens.size() == 2 &&
        innerTokens.front().kind == ExprTokenKind::Identifier &&
        innerTokens.back().kind == ExprTokenKind::End) {
      auto resolved = resolveFieldRefTokenText(
          context, innerTokens.front().text, absolutePos(exprStart));
      if (resolved.ok()) {
        return ParseStep<ExprPtr>{
            .value = std::make_unique<FieldRefExpr>(std::move(resolved.value))};
      }
    }

    ExprParseResult embedded =
        parseLibraryExpression(innerExprText, context.resolver);
    if (!embedded.ok()) {
      const int relativeErrorPos =
          embedded.error.position < 0 ? 0 : embedded.error.position;
      return makeErrorStep<ExprPtr>(embedded.error.message,
                                    absolutePos(exprStart + relativeErrorPos));
    }
    return ParseStep<ExprPtr>{.value = std::move(embedded.expr)};
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
    const int exprEnd = findInterpolationEnd(content, exprStart);
    if (exprEnd < 0) {
      return makeErrorStep<ExprPtr>(
          QStringLiteral("Unterminated interpolation `${...}`"),
          absolutePos(marker));
    }

    auto embedded = parseEmbeddedExpr(
        content.mid(exprStart, exprEnd - exprStart), exprStart);
    if (!embedded.ok()) {
      return embedded;
    }
    parts.push_back(std::move(embedded.value));
    cursor = exprEnd + 1;
  }

  if (parts.empty()) {
    return ParseStep<ExprPtr>{
        .value = std::make_unique<LiteralExpr>(
            ExprRuntimeValue::fromText(content.toStdString())),
        .nextIndex = index + 1};
  }

  return ParseStep<ExprPtr>{
      .value = std::make_unique<InterpolatedStringExpr>(std::move(parts)),
      .nextIndex = index + 1};
}

// Parses comparison suffix after an already-parsed left expression:
// `<left> <comparison-op> <right>`.
ParseStep<ExprPtr> parseExprValueComparisonSuffix(const ParseContext &context,
                                                  ParseStep<ExprPtr> left) {
  const int suffixIndex = left.nextIndex;
  const ExprToken &operatorToken = peek(context.tokens, suffixIndex);
  if (!isComparisonOperatorToken(operatorToken.kind)) {
    return left;
  }

  ValueType leftValueType = ValueType::Text;
  if (const auto *fieldExpr =
          dynamic_cast<const FieldRefExpr *>(left.value.get())) {
    leftValueType = fieldExpr->field.valueType;
  } else {
    const ExprStaticType leftType = inferExprStaticType(*left.value);
    if (leftType == ExprStaticType::Invalid) {
      return makeErrorStep<ExprPtr>(QStringLiteral("Invalid expression type"),
                                    operatorToken.start);
    }
    leftValueType = columnValueTypeFromExprType(leftType);
  }

  return parseComparisonTail(
      context, std::move(left.value), leftValueType, suffixIndex,
      operatorToken.start,
      QStringLiteral("Value is not valid for this expression type"));
}

// Shared parser for comparison tail:
// `<leftExpr> <comparison-op> <rightExpr-or-value>`.
ParseStep<ExprPtr> parseComparisonTail(const ParseContext &context,
                                       ExprPtr leftExpr,
                                       ValueType leftValueType,
                                       int operatorIndex, int errorPosition,
                                       const QString &invalidValueMessage) {
  auto opStep = parseComparisonOperator(context, operatorIndex);
  if (!opStep.ok()) {
    return makeErrorStep<ExprPtr>(opStep.error.message, opStep.error.position);
  }
  auto rightStep = parseComparisonRightExpr(context, opStep.nextIndex,
                                            *opStep.value, leftValueType,
                                            errorPosition, invalidValueMessage);
  if (!rightStep.ok()) {
    return rightStep;
  }

  return ParseStep<ExprPtr>{.value = std::make_unique<ComparisonExpr>(
                                std::move(leftExpr), std::move(opStep.value),
                                std::move(rightStep.value), leftValueType),
                            .nextIndex = rightStep.nextIndex};
}

// Parses field-led comparisons (`<field> <comparison-op> <right>`) with the
// left field resolved through the symbol resolver.
ParseStep<ExprPtr> parseComparisonExpr(const ParseContext &context, int index) {
  auto fieldStep = parseFieldRef(context, index);
  if (!fieldStep.ok()) {
    return makeErrorStep<ExprPtr>(fieldStep.error.message,
                                  fieldStep.error.position);
  }
  const std::string fieldName = fieldStep.value.exprFieldName;
  const ValueType leftValueType = fieldStep.value.valueType;
  return parseComparisonTail(
      context, std::make_unique<FieldRefExpr>(std::move(fieldStep.value)),
      leftValueType, fieldStep.nextIndex, peek(context.tokens, index).start,
      QStringLiteral("Value is not valid for field `%1`")
          .arg(QString::fromStdString(fieldName)));
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
  const auto symbol = context.resolver.lookup(normalizedField);
  if (!symbol) {
    return makeErrorStep<ExprFieldRef>(
        QStringLiteral("Unknown field `%1`").arg(fieldText), position);
  }
  return ParseStep<ExprFieldRef>{
      .value =
          ExprFieldRef{normalizedField, symbol->resolvedId, symbol->valueType}};
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

// Parses scalar/list literal values used by comparison operators (notably IN),
// producing ExprValue without requiring a full expression parse.
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
                                       const ExprSymbolResolver &resolver) {
  if (expressionText.trimmed().isEmpty()) {
    return ExprParseResult{
        .error = ExprParseError{.message = QStringLiteral("Expression cannot "
                                                          "be empty"),
                                .position = 0}};
  }
  const std::vector<ExprToken> tokens =
      tokenizeLibraryExpression(expressionText);
  const ParseContext context{expressionText, tokens, resolver};
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

ExprSymbolResolver::ExprSymbolResolver(std::vector<ExprSymbolInfo> symbols)
    : symbols_(std::move(symbols)) {}

std::optional<ExprSymbolInfo>
ExprSymbolResolver::lookup(std::string_view normalizedName) const {
  for (const ExprSymbolInfo &symbol : symbols_) {
    if (symbol.name == normalizedName) {
      return symbol;
    }
  }
  return std::nullopt;
}

std::vector<ExprSymbolInfo>
mergeExprSymbols(std::vector<ExprSymbolInfo> primary,
                 const std::vector<ExprSymbolInfo> &fallback) {
  primary.reserve(primary.size() + fallback.size());
  for (const ExprSymbolInfo &symbol : fallback) {
    const bool exists = std::any_of(primary.begin(), primary.end(),
                                    [&](const ExprSymbolInfo &existing) {
                                      return existing.name == symbol.name;
                                    });
    if (!exists) {
      primary.push_back(symbol);
    }
  }
  return primary;
}
