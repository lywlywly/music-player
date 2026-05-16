#ifndef LIBRARYEXPRESSION_OPS_H
#define LIBRARYEXPRESSION_OPS_H

#include "libraryexpression.h"
#include <QString>

bool exprValueMatchesFieldType(const ExprValue &value, ValueType valueType);
bool supportsRangeValueType(ValueType valueType);
bool isRangeBoundaryOrderValid(const ExprValue &value, ValueType valueType);
QString runtimeValueToQString(const ExprRuntimeValue &runtimeValue);

#endif // LIBRARYEXPRESSION_OPS_H
