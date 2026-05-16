#ifndef STATUSRUNTIMESYMBOLTABLE_H
#define STATUSRUNTIMESYMBOLTABLE_H

#include "columndefinition.h"
#include "exprsymbolinfo.h"
#include "fieldvalue.h"
#include <string_view>
#include <unordered_map>
#include <vector>

class StatusRuntimeSymbolTable {
public:
  StatusRuntimeSymbolTable();

  static const QString &defaultStatusBarExpression();
  static const QString &defaultWindowTitleExpression();
  // Returns runtime expression symbols in two forms per field:
  // - unqualified alias (e.g. `bitrate`)
  // - qualified alias (e.g. `status:bitrate`)
  // When merged as primary symbols, these aliases win collisions for
  // unqualified names in display-expression parsing.
  static const std::vector<ExprSymbolInfo> &expressionSymbols();

  void setIsPlaying(bool isPlaying);
  void setIsPaused(bool isPaused);
  void setPlaybackTimeSeconds(qint64 seconds);
  void setDurationSeconds(qint64 seconds);
  void setBitrateKbps(qint64 bitrateKbps);

  const FieldValue *fieldValue(std::string_view fieldId) const;

private:
  static qint64 clampNonNegative(qint64 value);
  static const std::vector<FieldDefinition> &fieldDefinitions();

  std::unordered_map<std::string, FieldValue> values_;
};

#endif // STATUSRUNTIMESYMBOLTABLE_H
