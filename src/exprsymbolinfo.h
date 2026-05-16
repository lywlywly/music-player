#ifndef EXPRSYMBOLINFO_H
#define EXPRSYMBOLINFO_H

#include "columndefinition.h"
#include <string>

// Minimal parser-facing symbol shape. Providers map their own metadata
// (columns/runtime symbols) into this type before expression parsing.
struct ExprSymbolInfo {
  std::string name;
  std::string resolvedId;
  ValueType valueType = ValueType::Text;
};

#endif // EXPRSYMBOLINFO_H
