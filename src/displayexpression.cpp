#include "displayexpression.h"

#include "columnregistry.h"
#include "libraryexpression_ops.h"
#include "statusruntimesymboltable.h"
#include <QSettings>
#include <string_view>

namespace {
// Reads `status:*` fields from the runtime symbols and every other field from
// the current song.
class DisplayExpressionEvalContext final : public LibraryExprEvalContext {
public:
  // song may be nullptr (nothing has played yet).
  DisplayExpressionEvalContext(const StatusRuntimeSymbolTable &runtimeSymbols,
                               const MSong *song)
      : runtimeSymbols_(runtimeSymbols), song_(song) {}

  const FieldValue *fieldValue(std::string_view fieldId) const override {
    if (fieldId.starts_with("status:")) {
      return runtimeSymbols_.fieldValue(fieldId);
    }
    return song_ ? findSongFieldValue(*song_, fieldId) : nullptr;
  }

private:
  const StatusRuntimeSymbolTable &runtimeSymbols_;
  const MSong *song_ = nullptr;
};

const char *settingsKey(DisplayExpressionKind kind) {
  switch (kind) {
  case DisplayExpressionKind::StatusBar:
    return "status_bar/expression";
  case DisplayExpressionKind::WindowTitle:
    return "window_title/expression";
  }
  Q_UNREACHABLE();
}
} // namespace

const QString &defaultDisplayExpression(DisplayExpressionKind kind) {
  static const QString kDefaultStatusBarExpression = QStringLiteral(
      "`"
      "${codec} | ${bitrate} kbps | ${sample_rate} Hz | ${channels} | "
      "${playback_time}${IF duration > 0 THEN ` / ${duration}` ELSE ''} | "
      "${IF ispaused IS true THEN 'Paused' ELSE IF isplaying IS true THEN "
      "'Playing' ELSE 'Stopped'}`");
  static const QString kDefaultWindowTitleExpression = QStringLiteral(
      "`${IF artist IS '' THEN title ELSE `${artist} - ${title}`}`");
  switch (kind) {
  case DisplayExpressionKind::StatusBar:
    return kDefaultStatusBarExpression;
  case DisplayExpressionKind::WindowTitle:
    return kDefaultWindowTitleExpression;
  }
  Q_UNREACHABLE();
}

QString effectiveDisplayExpression(DisplayExpressionKind kind,
                                   const QString &expression) {
  const QString trimmed = expression.trimmed();
  return trimmed.isEmpty() ? defaultDisplayExpression(kind) : trimmed;
}

QString loadDisplayExpressionSetting(DisplayExpressionKind kind) {
  QSettings settings;
  return effectiveDisplayExpression(
      kind, settings.value(settingsKey(kind)).toString());
}

QString saveDisplayExpressionSetting(DisplayExpressionKind kind,
                                     const QString &expression) {
  const QString effective = effectiveDisplayExpression(kind, expression);
  QSettings settings;
  settings.setValue(settingsKey(kind), effective);
  return effective;
}

ExprParseResult parseDisplayExpression(const QString &expression,
                                       const ColumnRegistry &registry) {
  const ExprSymbolResolver resolver(
      mergeExprSymbols(std::vector<ExprSymbolInfo>(
                           StatusRuntimeSymbolTable::expressionSymbols()),
                       registry.expressionSymbols()));
  return parseLibraryExpression(expression, resolver);
}

QString
evaluateDisplayExpression(const Expr &expr,
                          const StatusRuntimeSymbolTable &runtimeSymbols,
                          const MSong *song) {
  DisplayExpressionEvalContext context(runtimeSymbols, song);
  return runtimeValueToQString(expr.evaluateValue(context));
}
