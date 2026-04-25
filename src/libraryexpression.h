#ifndef LIBRARYEXPRESSION_H
#define LIBRARYEXPRESSION_H

#include "columndefinition.h"
#include "columnregistry.h"
#include "fieldvalue.h"
#include <QString>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

struct ExprValue {
  enum class Kind { Scalar, List, Range };

  Kind kind = Kind::Scalar;
  std::vector<std::string> values;

  bool isScalar() const { return kind == Kind::Scalar && values.size() == 1; }
  bool isList() const { return kind == Kind::List && !values.empty(); }
  bool isRange() const { return kind == Kind::Range && values.size() == 2; }
  const std::string &scalarText() const { return values.front(); }
  const std::string &rangeStart() const { return values.front(); }
  const std::string &rangeEnd() const { return values.back(); }
};

struct ExprFieldRef {
  std::string exprFieldName;
  std::string resolvedColumnId;
  ColumnValueType valueType = ColumnValueType::Text;
};

struct LibraryExprEvalContext {
  virtual ~LibraryExprEvalContext() = default;
  virtual const FieldValue *fieldValue(std::string_view fieldId) const = 0;
};

struct ExprRuntimeValue {
  std::variant<bool, std::string, double> value = false;

  static ExprRuntimeValue fromBool(bool booleanValue) {
    ExprRuntimeValue runtimeValue;
    runtimeValue.value = booleanValue;
    return runtimeValue;
  }

  static ExprRuntimeValue fromText(std::string textValue) {
    ExprRuntimeValue runtimeValue;
    runtimeValue.value = std::move(textValue);
    return runtimeValue;
  }

  static ExprRuntimeValue fromNumber(double numberValue) {
    ExprRuntimeValue runtimeValue;
    runtimeValue.value = numberValue;
    return runtimeValue;
  }

  bool isBool() const { return std::holds_alternative<bool>(value); }
  bool isText() const { return std::holds_alternative<std::string>(value); }
  bool isNumber() const { return std::holds_alternative<double>(value); }
  bool boolValueOrFalse() const {
    if (!isBool()) {
      return false;
    }
    return std::get<bool>(value);
  }
  const std::string &textValue() const { return std::get<std::string>(value); }
  double numberValue() const { return std::get<double>(value); }
};

struct ExprOperator {
  virtual ~ExprOperator() = default;
  virtual bool evaluate(const FieldValue &fieldValue,
                        const ExprValue &exprValue) const = 0;
  virtual bool equals(const ExprOperator &other) const = 0;
  virtual bool supportsValue(const ExprValue &exprValue) const = 0;
  virtual std::string displayName() const = 0;
};

inline bool operator==(const ExprOperator &left, const ExprOperator &right) {
  return left.equals(right);
}

using ExprOperatorPtr = std::unique_ptr<ExprOperator>;

struct IsOperator final : ExprOperator {
  bool evaluate(const FieldValue &fieldValue,
                const ExprValue &exprValue) const override;
  bool equals(const ExprOperator &other) const override;
  bool supportsValue(const ExprValue &exprValue) const override;
  std::string displayName() const override;
};

struct HasOperator final : ExprOperator {
  bool evaluate(const FieldValue &fieldValue,
                const ExprValue &exprValue) const override;
  bool equals(const ExprOperator &other) const override;
  bool supportsValue(const ExprValue &exprValue) const override;
  std::string displayName() const override;
};

struct InOperator final : ExprOperator {
  bool evaluate(const FieldValue &fieldValue,
                const ExprValue &exprValue) const override;
  bool equals(const ExprOperator &other) const override;
  bool supportsValue(const ExprValue &exprValue) const override;
  std::string displayName() const override;
};

struct LtOperator final : ExprOperator {
  bool evaluate(const FieldValue &fieldValue,
                const ExprValue &exprValue) const override;
  bool equals(const ExprOperator &other) const override;
  bool supportsValue(const ExprValue &exprValue) const override;
  std::string displayName() const override;
};

struct LteOperator final : ExprOperator {
  bool evaluate(const FieldValue &fieldValue,
                const ExprValue &exprValue) const override;
  bool equals(const ExprOperator &other) const override;
  bool supportsValue(const ExprValue &exprValue) const override;
  std::string displayName() const override;
};

struct GtOperator final : ExprOperator {
  bool evaluate(const FieldValue &fieldValue,
                const ExprValue &exprValue) const override;
  bool equals(const ExprOperator &other) const override;
  bool supportsValue(const ExprValue &exprValue) const override;
  std::string displayName() const override;
};

struct GteOperator final : ExprOperator {
  bool evaluate(const FieldValue &fieldValue,
                const ExprValue &exprValue) const override;
  bool equals(const ExprOperator &other) const override;
  bool supportsValue(const ExprValue &exprValue) const override;
  std::string displayName() const override;
};

struct Expr {
  virtual ~Expr() = default;
  virtual ExprRuntimeValue
  evaluateValue(const LibraryExprEvalContext &context) const = 0;
  bool evaluate(const LibraryExprEvalContext &context) const {
    return evaluateValue(context).boolValueOrFalse();
  }
  virtual bool equals(const Expr &other) const = 0;
};

inline bool operator==(const Expr &left, const Expr &right) {
  return left.equals(right);
}

using ExprPtr = std::unique_ptr<Expr>;

struct ComparisonExpr final : Expr {
  ExprPtr leftExpr;
  ExprFieldRef field;
  ExprOperatorPtr op;
  ExprValue value;
  bool hasFieldRef = false;

  ComparisonExpr(ExprFieldRef fieldRef, ExprOperatorPtr exprOp,
                 ExprValue exprValue);
  ComparisonExpr(ExprPtr leftExpression, ExprOperatorPtr exprOp,
                 ExprValue exprValue);
  ExprRuntimeValue
  evaluateValue(const LibraryExprEvalContext &context) const override;
  bool equals(const Expr &other) const override;
};

struct AndExpr final : Expr {
  ExprPtr left;
  ExprPtr right;

  AndExpr(ExprPtr leftExpr, ExprPtr rightExpr);
  ExprRuntimeValue
  evaluateValue(const LibraryExprEvalContext &context) const override;
  bool equals(const Expr &other) const override;
};

struct OrExpr final : Expr {
  ExprPtr left;
  ExprPtr right;

  OrExpr(ExprPtr leftExpr, ExprPtr rightExpr);
  ExprRuntimeValue
  evaluateValue(const LibraryExprEvalContext &context) const override;
  bool equals(const Expr &other) const override;
};

struct NotExpr final : Expr {
  ExprPtr child;

  explicit NotExpr(ExprPtr childExpr);
  ExprRuntimeValue
  evaluateValue(const LibraryExprEvalContext &context) const override;
  bool equals(const Expr &other) const override;
};

struct LiteralExpr final : Expr {
  ExprRuntimeValue value;

  explicit LiteralExpr(ExprRuntimeValue runtimeValue);
  ExprRuntimeValue
  evaluateValue(const LibraryExprEvalContext &context) const override;
  bool equals(const Expr &other) const override;
};

struct IfExpr final : Expr {
  ExprPtr condition;
  ExprPtr thenExpr;
  ExprPtr elseExpr;

  IfExpr(ExprPtr conditionExpr, ExprPtr thenBranch, ExprPtr elseBranch);
  ExprRuntimeValue
  evaluateValue(const LibraryExprEvalContext &context) const override;
  bool equals(const Expr &other) const override;
};

struct ExprParseError {
  QString message;
  int position = -1;
};

struct ExprParseResult {
  ExprPtr expr;
  ExprParseError error;

  bool ok() const { return expr != nullptr; }
};

enum class ExprStaticType { Invalid, Bool, Text, Number };
ExprStaticType inferExprStaticType(const Expr &expr);

ExprParseResult parseLibraryExpression(const QString &expressionText,
                                       const ColumnRegistry &registry);

#endif // LIBRARYEXPRESSION_H
