#include "libraryexpression_parser.h"

#include "libraryexpression_ops.h"
#include "libraryexpression_tokenizer.h"
#include "utils.h"

#include <functional>
#include <tuple>
#include <utility>

namespace {
bool splitRangeBoundaries(std::string_view raw, std::string &start,
                          std::string &end) {
  const QString qRaw = QString::fromStdString(std::string(raw));
  const int sep = qRaw.indexOf(QStringLiteral(".."));
  if (sep < 0 || sep != qRaw.lastIndexOf(QStringLiteral(".."))) {
    return false;
  }

  const QString left = qRaw.left(sep).trimmed();
  const QString right = qRaw.mid(sep + 2).trimmed();
  if (left.isEmpty() || right.isEmpty()) {
    return false;
  }

  start = left.toStdString();
  end = right.toStdString();
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
  ParseStep<T> step;
  step.error = {message, position};
  return step;
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

    ParseStep<U> mapped;
    mapped.value = std::invoke(transform, std::move(step.value));
    mapped.nextIndex = step.nextIndex;
    return mapped;
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

    ParseStep<std::tuple<First>> result;
    result.value = std::make_tuple(std::move(step.value));
    result.nextIndex = step.nextIndex;
    return result;
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

        ParseStep<std::tuple<First, Second, Rest...>> step;
        step.value = std::tuple_cat(std::make_tuple(std::move(left.value)),
                                    std::move(right.value));
        step.nextIndex = right.nextIndex;
        return step;
      };
}

template <typename Combine>
ParserFn<ExprPtr> chainLeft(ParserFn<ExprPtr> operand,
                            ExprTokenKind operatorKind, Combine combine) {
  return
      [operand = std::move(operand), operatorKind,
       combine = std::move(combine)](const ParseContext &context,
                                     int index) mutable -> ParseStep<ExprPtr> {
        auto left = operand(context, index);
        if (!left.ok()) {
          return left;
        }

        while (peek(context.tokens, left.nextIndex).kind == operatorKind) {
          auto right = operand(context, left.nextIndex + 1);
          if (!right.ok()) {
            return right;
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
ParseStep<ExprPtr> parseComparisonExpr(const ParseContext &context, int index);
ParseStep<ExprFieldRef> parseFieldRef(const ParseContext &context, int index);
ParseStep<ExprOperatorPtr> parseComparisonOperator(const ParseContext &context,
                                                   int index);
ParseStep<ExprValue> parseValue(const ParseContext &context, int index);
ParseStep<ExprValue> parseListValue(const ParseContext &context, int index);

ParseStep<ExprPtr> parseOr(const ParseContext &context, int index) {
  static const auto parser = chainLeft(
      ParserFn<ExprPtr>(parseAnd), ExprTokenKind::KeywordOr,
      [](ExprPtr left, ExprPtr right) {
        return std::make_unique<OrExpr>(std::move(left), std::move(right));
      });
  return parser(context, index);
}

ParseStep<ExprPtr> parseAnd(const ParseContext &context, int index) {
  static const auto parser = chainLeft(
      ParserFn<ExprPtr>(parseUnary), ExprTokenKind::KeywordAnd,
      [](ExprPtr left, ExprPtr right) {
        return std::make_unique<AndExpr>(std::move(left), std::move(right));
      });
  return parser(context, index);
}

ParseStep<ExprPtr> parseUnary(const ParseContext &context, int index) {
  const ExprToken &token = peek(context.tokens, index);
  if (token.kind == ExprTokenKind::KeywordNot) {
    auto child = parseUnary(context, index + 1);
    if (!child.ok()) {
      return child;
    }

    ParseStep<ExprPtr> step;
    step.value = std::make_unique<NotExpr>(std::move(child.value));
    step.nextIndex = child.nextIndex;
    return step;
  }
  return parsePrimary(context, index);
}

ParseStep<ExprPtr> parsePrimary(const ParseContext &context, int index) {
  if (peek(context.tokens, index).kind == ExprTokenKind::LParen) {
    return parseGroupedExpr(context, index);
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

  const std::string normalizedField = util::normalizedText(fieldToken.text);

  const ColumnDefinition *direct =
      context.registry.findColumn(QString::fromStdString(normalizedField));
  if (direct) {
    if (direct->source != ColumnSource::SongAttribute) {
      return makeErrorStep<ExprFieldRef>(
          QStringLiteral("Field `%1` is not searchable")
              .arg(QString::fromStdString(fieldToken.text)),
          fieldToken.start);
    }

    ParseStep<ExprFieldRef> step;
    step.value = {normalizedField, normalizedField, direct->valueType};
    step.nextIndex = index + 1;
    return step;
  }

  const std::string customColumnId = std::string("attr:") + normalizedField;
  const ColumnDefinition *custom =
      context.registry.findColumn(QString::fromStdString(customColumnId));
  if (!custom || custom->source != ColumnSource::SongAttribute) {
    return makeErrorStep<ExprFieldRef>(
        QStringLiteral("Unknown field `%1`")
            .arg(QString::fromStdString(fieldToken.text)),
        fieldToken.start);
  }

  ParseStep<ExprFieldRef> step;
  step.value = {normalizedField, customColumnId, custom->valueType};
  step.nextIndex = index + 1;
  return step;
}

ParseStep<ExprOperatorPtr> parseComparisonOperator(const ParseContext &context,
                                                   int index) {
  const ExprToken &opToken = peek(context.tokens, index);
  if (opToken.kind == ExprTokenKind::KeywordIs ||
      opToken.kind == ExprTokenKind::OpEq) {
    ParseStep<ExprOperatorPtr> step;
    step.value = std::make_unique<IsOperator>();
    step.nextIndex = index + 1;
    return step;
  }
  if (opToken.kind == ExprTokenKind::KeywordHas) {
    ParseStep<ExprOperatorPtr> step;
    step.value = std::make_unique<HasOperator>();
    step.nextIndex = index + 1;
    return step;
  }
  if (opToken.kind == ExprTokenKind::KeywordIn) {
    ParseStep<ExprOperatorPtr> step;
    step.value = std::make_unique<InOperator>();
    step.nextIndex = index + 1;
    return step;
  }
  if (opToken.kind == ExprTokenKind::OpLt) {
    ParseStep<ExprOperatorPtr> step;
    step.value = std::make_unique<LtOperator>();
    step.nextIndex = index + 1;
    return step;
  }
  if (opToken.kind == ExprTokenKind::OpLte) {
    ParseStep<ExprOperatorPtr> step;
    step.value = std::make_unique<LteOperator>();
    step.nextIndex = index + 1;
    return step;
  }
  if (opToken.kind == ExprTokenKind::OpGt) {
    ParseStep<ExprOperatorPtr> step;
    step.value = std::make_unique<GtOperator>();
    step.nextIndex = index + 1;
    return step;
  }
  if (opToken.kind == ExprTokenKind::OpGte) {
    ParseStep<ExprOperatorPtr> step;
    step.value = std::make_unique<GteOperator>();
    step.nextIndex = index + 1;
    return step;
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
            .arg(QString::fromStdString(token.text)),
        token.start);
  }

  if (token.kind == ExprTokenKind::StringLiteral) {
    ParseStep<ExprValue> step;
    step.value.kind = ExprValue::Kind::Scalar;
    step.value.values.push_back(token.text);
    step.nextIndex = index + 1;
    return step;
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

  ParseStep<ExprValue> step;
  step.value.kind = ExprValue::Kind::Scalar;
  step.value.values.push_back(rawValue.toStdString());
  step.nextIndex = index;
  return step;
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

      ParseStep<ExprValue> step;
      step.value = std::move(value);
      step.nextIndex = index + 1;
      return step;
    }

    if (token.kind != ExprTokenKind::Identifier &&
        token.kind != ExprTokenKind::StringLiteral) {
      return makeErrorStep<ExprValue>(QStringLiteral("Expected a list value"),
                                      token.start);
    }

    if (token.kind == ExprTokenKind::StringLiteral) {
      sawQuotedItem = true;
      value.values.push_back(token.text);
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
    ExprParseResult result;
    result.error = {QStringLiteral("Expression cannot be empty"), 0};
    return result;
  }

  const std::vector<ExprToken> tokens =
      tokenizeLibraryExpression(expressionText);
  const ParseContext context{expressionText, registry, tokens};
  auto parsed = parseOr(context, 0);
  if (!parsed.ok()) {
    ExprParseResult result;
    result.error = parsed.error;
    return result;
  }

  const ExprToken &nextToken = peek(tokens, parsed.nextIndex);
  if (nextToken.kind != ExprTokenKind::End) {
    ExprParseResult result;
    result.error = {QStringLiteral("Unexpected token `%1`")
                        .arg(QString::fromStdString(nextToken.text)),
                    nextToken.start};
    return result;
  }

  ExprParseResult result;
  result.expr = std::move(parsed.value);
  return result;
}
