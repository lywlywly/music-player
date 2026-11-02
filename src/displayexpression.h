#ifndef DISPLAYEXPRESSION_H
#define DISPLAYEXPRESSION_H

#include "libraryexpression_parser.h"
#include "songlibrary.h"
#include <QString>

class ColumnRegistry;
class StatusRuntimeSymbolTable;

// Shared rules for the user-editable display expressions (status bar and
// window title), used by NowPlayingDisplay and SettingsDialog.

enum class DisplayExpressionKind { StatusBar, WindowTitle };

const QString &defaultDisplayExpression(DisplayExpressionKind kind);

// The expression trimmed, or the default one when it is blank.
QString effectiveDisplayExpression(DisplayExpressionKind kind,
                                   const QString &expression);

// The stored expression (effective), or the default when none is stored.
QString loadDisplayExpressionSetting(DisplayExpressionKind kind);
// Stores the effective expression and returns it.
QString saveDisplayExpressionSetting(DisplayExpressionKind kind,
                                     const QString &expression);

// Parses with runtime `status:*` symbols ahead of the registry symbols.
ExprParseResult parseDisplayExpression(const QString &expression,
                                       const ColumnRegistry &registry);

// song may be nullptr (nothing has played yet).
QString
evaluateDisplayExpression(const Expr &expr,
                          const StatusRuntimeSymbolTable &runtimeSymbols,
                          const MSong *song);

#endif // DISPLAYEXPRESSION_H
