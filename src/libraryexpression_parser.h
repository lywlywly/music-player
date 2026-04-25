#ifndef LIBRARYEXPRESSION_PARSER_H
#define LIBRARYEXPRESSION_PARSER_H

#include "libraryexpression.h"

ExprParseResult parseLibraryExpression(const QString &expressionText,
                                       const ColumnRegistry &registry);

#endif // LIBRARYEXPRESSION_PARSER_H
