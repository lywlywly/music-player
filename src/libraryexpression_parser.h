#ifndef LIBRARYEXPRESSION_PARSER_H
#define LIBRARYEXPRESSION_PARSER_H

#include "exprsymbolinfo.h"
#include "libraryexpression.h"
#include <QString>
#include <optional>
#include <string_view>
#include <vector>

struct ExprParseError {
  QString message;
  int position = -1;
};

struct ExprParseResult {
  ExprPtr expr;
  ExprParseError error;

  bool ok() const { return expr != nullptr; }
};

class ExprSymbolResolver {
public:
  explicit ExprSymbolResolver(std::vector<ExprSymbolInfo> symbols);
  ~ExprSymbolResolver() = default;

  std::optional<ExprSymbolInfo> lookup(std::string_view normalizedName) const;

private:
  std::vector<ExprSymbolInfo> symbols_;
};

std::vector<ExprSymbolInfo>
mergeExprSymbols(std::vector<ExprSymbolInfo> primary,
                 const std::vector<ExprSymbolInfo> &fallback);

ExprParseResult parseLibraryExpression(const QString &expressionText,
                                       const ExprSymbolResolver &resolver);

#endif // LIBRARYEXPRESSION_PARSER_H
