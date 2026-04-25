#ifndef LIBRARYEXPRESSION_TOKENIZER_H
#define LIBRARYEXPRESSION_TOKENIZER_H

#include <QString>
#include <string>
#include <vector>

enum class ExprTokenKind {
  Invalid,
  Identifier,
  StringLiteral,
  KeywordIs,
  KeywordAnd,
  KeywordOr,
  KeywordNot,
  KeywordHas,
  KeywordIn,
  KeywordIf,
  KeywordThen,
  KeywordElse,
  OpLt,
  OpLte,
  OpGt,
  OpGte,
  OpEq,
  LBracket,
  RBracket,
  Comma,
  LParen,
  RParen,
  End,
};

struct ExprToken {
  ExprTokenKind kind = ExprTokenKind::End;
  std::string text;
  int start = -1;
  int end = -1;

  bool operator==(const ExprToken &other) const {
    return kind == other.kind && text == other.text && start == other.start &&
           end == other.end;
  }
};

std::vector<ExprToken> tokenizeLibraryExpression(const QString &expressionText);

#endif // LIBRARYEXPRESSION_TOKENIZER_H
