#ifndef FIELDFORMATTER_H
#define FIELDFORMATTER_H

#include "columndefinition.h"
#include "fieldvalue.h"

// Returns -1/0/1 like strcmp when both values can be compared by type.
// Returns false in ok when conversion fails.
int compareFieldText(const std::string &left, const std::string &right,
                     ValueType type, bool &ok);
int compareFieldValues(const FieldValue &left, const FieldValue &right,
                       ValueType type, bool &ok);

#endif // FIELDFORMATTER_H
