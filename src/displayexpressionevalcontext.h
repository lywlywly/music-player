#ifndef DISPLAYEXPRESSIONEVALCONTEXT_H
#define DISPLAYEXPRESSIONEVALCONTEXT_H

#include "libraryexpression.h"
#include "statusruntimesymboltable.h"
#include <string_view>
#include <unordered_map>

class DisplayExpressionEvalContext final : public LibraryExprEvalContext {
public:
  DisplayExpressionEvalContext(
      const StatusRuntimeSymbolTable &runtimeSymbols,
      const std::unordered_map<std::string, FieldValue> *song)
      : runtimeSymbols_(runtimeSymbols), song_(song) {}

  const FieldValue *fieldValue(std::string_view fieldId) const override {
    if (fieldId.starts_with("status:")) {
      return runtimeSymbols_.fieldValue(fieldId);
    }
    if (!song_) {
      return nullptr;
    }
    auto it = song_->find(songKeyForFieldId(fieldId));
    if (it == song_->end()) {
      return nullptr;
    }
    return &it->second;
  }

private:
  static std::string songKeyForFieldId(std::string_view fieldId) {
    if (fieldId.starts_with("builtin:")) {
      return std::string(fieldId.substr(8));
    }
    return std::string(fieldId);
  }

  const StatusRuntimeSymbolTable &runtimeSymbols_;
  const std::unordered_map<std::string, FieldValue> *song_ = nullptr;
};

#endif // DISPLAYEXPRESSIONEVALCONTEXT_H
