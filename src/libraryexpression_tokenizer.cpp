#include "libraryexpression_tokenizer.h"

namespace {
bool isOperatorStart(QChar ch) { return ch == '<' || ch == '>' || ch == '='; }

bool isTokenBoundaryStart(QChar ch) {
  return isOperatorStart(ch) || ch == '(' || ch == ')' || ch == '[' ||
         ch == ']' || ch == ',';
}
} // namespace

std::vector<ExprToken>
tokenizeLibraryExpression(const QString &expressionText) {
  std::vector<ExprToken> tokens;
  const int length = expressionText.size();
  int i = 0;
  while (i < length) {
    if (expressionText.at(i).isSpace()) {
      ++i;
      continue;
    }

    const int start = i;
    if (expressionText.at(i) == '"' || expressionText.at(i) == '\'') {
      const QChar quote = expressionText.at(i);
      ++i;
      const int contentStart = i;
      while (i < length && expressionText.at(i) != quote) {
        ++i;
      }
      if (i >= length) {
        tokens.push_back(
            {ExprTokenKind::Invalid, expressionText.mid(start), start, length});
        break;
      }
      tokens.push_back({ExprTokenKind::StringLiteral,
                        expressionText.mid(contentStart, i - contentStart),
                        start, i + 1});
      ++i;
      continue;
    }
    if (expressionText.at(i) == '(') {
      ++i;
      tokens.push_back({ExprTokenKind::LParen, QStringLiteral("("), start, i});
      continue;
    }
    if (expressionText.at(i) == '[') {
      ++i;
      tokens.push_back(
          {ExprTokenKind::LBracket, QStringLiteral("["), start, i});
      continue;
    }
    if (expressionText.at(i) == ']') {
      ++i;
      tokens.push_back(
          {ExprTokenKind::RBracket, QStringLiteral("]"), start, i});
      continue;
    }
    if (expressionText.at(i) == ',') {
      ++i;
      tokens.push_back({ExprTokenKind::Comma, QStringLiteral(","), start, i});
      continue;
    }
    if (expressionText.at(i) == ')') {
      ++i;
      tokens.push_back({ExprTokenKind::RParen, QStringLiteral(")"), start, i});
      continue;
    }
    if (expressionText.at(i) == '<') {
      ++i;
      if (i < length && expressionText.at(i) == '=') {
        ++i;
        tokens.push_back(
            {ExprTokenKind::OpLte, QStringLiteral("<="), start, i});
      } else {
        tokens.push_back({ExprTokenKind::OpLt, QStringLiteral("<"), start, i});
      }
      continue;
    }
    if (expressionText.at(i) == '>') {
      ++i;
      if (i < length && expressionText.at(i) == '=') {
        ++i;
        tokens.push_back(
            {ExprTokenKind::OpGte, QStringLiteral(">="), start, i});
      } else {
        tokens.push_back({ExprTokenKind::OpGt, QStringLiteral(">"), start, i});
      }
      continue;
    }
    if (expressionText.at(i) == '=') {
      ++i;
      tokens.push_back({ExprTokenKind::OpEq, QStringLiteral("="), start, i});
      continue;
    }

    while (i < length && !expressionText.at(i).isSpace() &&
           !isTokenBoundaryStart(expressionText.at(i))) {
      ++i;
    }

    const QString tokenText = expressionText.mid(start, i - start);
    const QString upper = tokenText.toUpper();
    ExprTokenKind kind = ExprTokenKind::Identifier;
    if (upper == QStringLiteral("IS")) {
      kind = ExprTokenKind::KeywordIs;
    } else if (upper == QStringLiteral("AND")) {
      kind = ExprTokenKind::KeywordAnd;
    } else if (upper == QStringLiteral("OR")) {
      kind = ExprTokenKind::KeywordOr;
    } else if (upper == QStringLiteral("NOT")) {
      kind = ExprTokenKind::KeywordNot;
    } else if (upper == QStringLiteral("HAS")) {
      kind = ExprTokenKind::KeywordHas;
    } else if (upper == QStringLiteral("IN")) {
      kind = ExprTokenKind::KeywordIn;
    } else if (upper == QStringLiteral("IF")) {
      kind = ExprTokenKind::KeywordIf;
    } else if (upper == QStringLiteral("THEN")) {
      kind = ExprTokenKind::KeywordThen;
    } else if (upper == QStringLiteral("ELSE")) {
      kind = ExprTokenKind::KeywordElse;
    }
    tokens.push_back({kind, tokenText, start, i});
  }

  tokens.push_back({ExprTokenKind::End, QString(), length, length});
  return tokens;
}
