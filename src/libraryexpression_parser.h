#ifndef LIBRARYEXPRESSION_PARSER_H
#define LIBRARYEXPRESSION_PARSER_H

#include "libraryexpression.h"
#include <QString>

class ColumnRegistry;

struct ExprParseError {
  QString message;
  int position = -1;
};

struct ExprParseResult {
  ExprPtr expr;
  ExprParseError error;

  bool ok() const { return expr != nullptr; }
};

ExprParseResult parseLibraryExpression(const QString &expressionText,
                                       const ColumnRegistry &registry);

#endif // LIBRARYEXPRESSION_PARSER_H
