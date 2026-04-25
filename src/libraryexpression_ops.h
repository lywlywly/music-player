#ifndef LIBRARYEXPRESSION_OPS_H
#define LIBRARYEXPRESSION_OPS_H

#include "libraryexpression.h"

bool exprValueMatchesFieldType(const ExprValue &value,
                               ColumnValueType valueType);
bool supportsRangeValueType(ColumnValueType valueType);
bool isRangeBoundaryOrderValid(const ExprValue &value,
                               ColumnValueType valueType);

#endif // LIBRARYEXPRESSION_OPS_H
