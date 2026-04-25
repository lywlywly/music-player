#include "libraryexpression_ops.h"
#include "utils.h"

#include <QStringList>

namespace {
int compareFieldValue(const FieldValue &fieldValue, std::string_view exprValue);
FieldValue fieldValueFromRuntime(const ExprRuntimeValue &runtimeValue);

bool fieldHasValue(const std::string &fieldText, const std::string &exprValue) {
  const QStringList parts =
      QString::fromStdString(fieldText).split(QStringLiteral(" / "));
  const QString normalizedExpression =
      util::normalizedText(QString::fromStdString(exprValue));
  for (const QString &part : parts) {
    if (util::normalizedText(part) == normalizedExpression) {
      return true;
    }
  }
  return false;
}

bool fieldHasTypedValue(const FieldValue &fieldValue,
                        const std::string &exprValue) {
  if (fieldValue.text.empty()) {
    return false;
  }

  const QStringList parts =
      QString::fromStdString(fieldValue.text).split(QStringLiteral(" / "));
  for (const QString &part : parts) {
    const std::string partText = part.trimmed().toStdString();
    if (partText.empty()) {
      continue;
    }
    if (!FieldValue::canConvert(partText, fieldValue.type)) {
      continue;
    }
    const FieldValue partValue(partText, fieldValue.type);
    if (compareFieldValue(partValue, exprValue) == 0) {
      return true;
    }
  }
  return false;
}

int compareFieldValue(const FieldValue &fieldValue,
                      std::string_view exprValue) {
  const std::string expressionText(exprValue);
  const FieldValue converted(expressionText, fieldValue.type);

  switch (fieldValue.type) {
  case ColumnValueType::Number:
    if (fieldValue.typed.number < converted.typed.number) {
      return -1;
    }
    if (fieldValue.typed.number > converted.typed.number) {
      return 1;
    }
    return 0;
  case ColumnValueType::DateTime:
    if (fieldValue.typed.epochMs < converted.typed.epochMs) {
      return -1;
    }
    if (fieldValue.typed.epochMs > converted.typed.epochMs) {
      return 1;
    }
    return 0;
  case ColumnValueType::Boolean:
    if (fieldValue.typed.boolean == converted.typed.boolean) {
      return 0;
    }
    return fieldValue.typed.boolean ? 1 : -1;
  case ColumnValueType::Text:
  default: {
    const std::string normalizedField = util::normalizedText(fieldValue.text);
    const std::string normalizedExpression =
        util::normalizedText(expressionText);
    if (normalizedField < normalizedExpression) {
      return -1;
    }
    if (normalizedField > normalizedExpression) {
      return 1;
    }
    return 0;
  }
  }
}

FieldValue fieldValueFromRuntime(const ExprRuntimeValue &runtimeValue) {
  if (runtimeValue.isBool()) {
    return FieldValue(std::get<bool>(runtimeValue.value) ? "true" : "false",
                      ColumnValueType::Boolean);
  }
  if (runtimeValue.isNumber()) {
    return FieldValue(
        QString::number(std::get<double>(runtimeValue.value), 'g', 17)
            .toStdString(),
        ColumnValueType::Number);
  }
  return FieldValue(std::get<std::string>(runtimeValue.value),
                    ColumnValueType::Text);
}
} // namespace

bool exprValueMatchesFieldType(const ExprValue &value,
                               ColumnValueType valueType) {
  for (const std::string &item : value.values) {
    if (!FieldValue::canConvert(item, valueType)) {
      return false;
    }
  }
  return true;
}

bool supportsRangeValueType(ColumnValueType valueType) {
  return valueType == ColumnValueType::Number ||
         valueType == ColumnValueType::DateTime;
}

bool isRangeBoundaryOrderValid(const ExprValue &value,
                               ColumnValueType valueType) {
  if (!value.isRange()) {
    return true;
  }
  const FieldValue lower(value.rangeStart(), valueType);
  return compareFieldValue(lower, value.rangeEnd()) <= 0;
}

bool IsOperator::evaluate(const FieldValue &fieldValue,
                          const ExprValue &exprValue) const {
  if (fieldValue.type != ColumnValueType::Text && fieldValue.text.empty()) {
    return false;
  }
  return compareFieldValue(fieldValue, exprValue.scalarText()) == 0;
}

bool IsOperator::equals(const ExprOperator &other) const {
  return dynamic_cast<const IsOperator *>(&other) != nullptr;
}

bool IsOperator::supportsValue(const ExprValue &exprValue) const {
  return exprValue.isScalar();
}

std::string IsOperator::displayName() const { return "IS"; }

bool HasOperator::evaluate(const FieldValue &fieldValue,
                           const ExprValue &exprValue) const {
  if (fieldValue.type != ColumnValueType::Text) {
    return fieldHasTypedValue(fieldValue, exprValue.scalarText());
  }
  return fieldHasValue(fieldValue.text, exprValue.scalarText());
}

bool HasOperator::equals(const ExprOperator &other) const {
  return dynamic_cast<const HasOperator *>(&other) != nullptr;
}

bool HasOperator::supportsValue(const ExprValue &exprValue) const {
  return exprValue.isScalar();
}

std::string HasOperator::displayName() const { return "HAS"; }

bool InOperator::evaluate(const FieldValue &fieldValue,
                          const ExprValue &exprValue) const {
  if (exprValue.isRange()) {
    if (fieldValue.text.empty()) {
      return false;
    }
    return compareFieldValue(fieldValue, exprValue.rangeStart()) >= 0 &&
           compareFieldValue(fieldValue, exprValue.rangeEnd()) <= 0;
  }

  const std::string normalizedField = util::normalizedText(fieldValue.text);
  for (const std::string &candidate : exprValue.values) {
    if (normalizedField == util::normalizedText(candidate)) {
      return true;
    }
  }
  return false;
}

bool InOperator::equals(const ExprOperator &other) const {
  return dynamic_cast<const InOperator *>(&other) != nullptr;
}

bool InOperator::supportsValue(const ExprValue &exprValue) const {
  return exprValue.isList() || exprValue.isRange();
}

std::string InOperator::displayName() const { return "IN"; }

bool LtOperator::evaluate(const FieldValue &fieldValue,
                          const ExprValue &exprValue) const {
  return !fieldValue.text.empty() &&
         compareFieldValue(fieldValue, exprValue.scalarText()) < 0;
}

bool LtOperator::equals(const ExprOperator &other) const {
  return dynamic_cast<const LtOperator *>(&other) != nullptr;
}

bool LtOperator::supportsValue(const ExprValue &exprValue) const {
  return exprValue.isScalar();
}

std::string LtOperator::displayName() const { return "<"; }

bool LteOperator::evaluate(const FieldValue &fieldValue,
                           const ExprValue &exprValue) const {
  return !fieldValue.text.empty() &&
         compareFieldValue(fieldValue, exprValue.scalarText()) <= 0;
}

bool LteOperator::equals(const ExprOperator &other) const {
  return dynamic_cast<const LteOperator *>(&other) != nullptr;
}

bool LteOperator::supportsValue(const ExprValue &exprValue) const {
  return exprValue.isScalar();
}

std::string LteOperator::displayName() const { return "<="; }

bool GtOperator::evaluate(const FieldValue &fieldValue,
                          const ExprValue &exprValue) const {
  return !fieldValue.text.empty() &&
         compareFieldValue(fieldValue, exprValue.scalarText()) > 0;
}

bool GtOperator::equals(const ExprOperator &other) const {
  return dynamic_cast<const GtOperator *>(&other) != nullptr;
}

bool GtOperator::supportsValue(const ExprValue &exprValue) const {
  return exprValue.isScalar();
}

std::string GtOperator::displayName() const { return ">"; }

bool GteOperator::evaluate(const FieldValue &fieldValue,
                           const ExprValue &exprValue) const {
  return !fieldValue.text.empty() &&
         compareFieldValue(fieldValue, exprValue.scalarText()) >= 0;
}

bool GteOperator::equals(const ExprOperator &other) const {
  return dynamic_cast<const GteOperator *>(&other) != nullptr;
}

bool GteOperator::supportsValue(const ExprValue &exprValue) const {
  return exprValue.isScalar();
}

std::string GteOperator::displayName() const { return ">="; }

ComparisonExpr::ComparisonExpr(ExprFieldRef fieldRef, ExprOperatorPtr exprOp,
                               ExprValue exprValue)
    : field(std::move(fieldRef)), op(std::move(exprOp)),
      value(std::move(exprValue)), hasFieldRef(true) {}

ComparisonExpr::ComparisonExpr(ExprPtr leftExpression, ExprOperatorPtr exprOp,
                               ExprValue exprValue)
    : leftExpr(std::move(leftExpression)), op(std::move(exprOp)),
      value(std::move(exprValue)), hasFieldRef(false) {}

ExprRuntimeValue
ComparisonExpr::evaluateValue(const LibraryExprEvalContext &context) const {
  if (hasFieldRef) {
    const FieldValue *fieldValue = context.fieldValue(field.resolvedColumnId);
    if (!fieldValue) {
      return ExprRuntimeValue::fromBool(false);
    }
    return ExprRuntimeValue::fromBool(op->evaluate(*fieldValue, value));
  }

  const ExprRuntimeValue leftValue = leftExpr->evaluateValue(context);
  const FieldValue convertedLeft = fieldValueFromRuntime(leftValue);
  return ExprRuntimeValue::fromBool(op->evaluate(convertedLeft, value));
}

bool ComparisonExpr::equals(const Expr &other) const {
  auto comparison = dynamic_cast<const ComparisonExpr *>(&other);
  if (!comparison) {
    return false;
  }
  if (hasFieldRef != comparison->hasFieldRef) {
    return false;
  }
  if (hasFieldRef) {
    if (field.exprFieldName != comparison->field.exprFieldName ||
        field.resolvedColumnId != comparison->field.resolvedColumnId ||
        field.valueType != comparison->field.valueType) {
      return false;
    }
  } else {
    if (!leftExpr->equals(*comparison->leftExpr)) {
      return false;
    }
  }
  return *op == *comparison->op && value.kind == comparison->value.kind &&
         value.values == comparison->value.values;
}

AndExpr::AndExpr(ExprPtr leftExpr, ExprPtr rightExpr)
    : left(std::move(leftExpr)), right(std::move(rightExpr)) {}

ExprRuntimeValue
AndExpr::evaluateValue(const LibraryExprEvalContext &context) const {
  return ExprRuntimeValue::fromBool(left->evaluate(context) &&
                                    right->evaluate(context));
}

bool AndExpr::equals(const Expr &other) const {
  auto andExpr = dynamic_cast<const AndExpr *>(&other);
  if (!andExpr) {
    return false;
  }
  return left->equals(*andExpr->left) && right->equals(*andExpr->right);
}

OrExpr::OrExpr(ExprPtr leftExpr, ExprPtr rightExpr)
    : left(std::move(leftExpr)), right(std::move(rightExpr)) {}

ExprRuntimeValue
OrExpr::evaluateValue(const LibraryExprEvalContext &context) const {
  return ExprRuntimeValue::fromBool(left->evaluate(context) ||
                                    right->evaluate(context));
}

bool OrExpr::equals(const Expr &other) const {
  auto orExpr = dynamic_cast<const OrExpr *>(&other);
  if (!orExpr) {
    return false;
  }
  return left->equals(*orExpr->left) && right->equals(*orExpr->right);
}

NotExpr::NotExpr(ExprPtr childExpr) : child(std::move(childExpr)) {}

ExprRuntimeValue
NotExpr::evaluateValue(const LibraryExprEvalContext &context) const {
  return ExprRuntimeValue::fromBool(!child->evaluate(context));
}

bool NotExpr::equals(const Expr &other) const {
  auto notExpr = dynamic_cast<const NotExpr *>(&other);
  if (!notExpr) {
    return false;
  }
  return child->equals(*notExpr->child);
}

LiteralExpr::LiteralExpr(ExprRuntimeValue runtimeValue)
    : value(std::move(runtimeValue)) {}

ExprRuntimeValue
LiteralExpr::evaluateValue(const LibraryExprEvalContext &) const {
  return value;
}

bool LiteralExpr::equals(const Expr &other) const {
  const auto *literal = dynamic_cast<const LiteralExpr *>(&other);
  if (!literal) {
    return false;
  }
  return value.value == literal->value.value;
}

IfExpr::IfExpr(ExprPtr conditionExpr, ExprPtr thenBranch, ExprPtr elseBranch)
    : condition(std::move(conditionExpr)), thenExpr(std::move(thenBranch)),
      elseExpr(std::move(elseBranch)) {}

ExprRuntimeValue
IfExpr::evaluateValue(const LibraryExprEvalContext &context) const {
  if (condition->evaluate(context)) {
    return thenExpr->evaluateValue(context);
  }
  return elseExpr->evaluateValue(context);
}

bool IfExpr::equals(const Expr &other) const {
  const auto *ifExpr = dynamic_cast<const IfExpr *>(&other);
  if (!ifExpr) {
    return false;
  }
  return condition->equals(*ifExpr->condition) &&
         thenExpr->equals(*ifExpr->thenExpr) &&
         elseExpr->equals(*ifExpr->elseExpr);
}

ExprStaticType inferExprStaticType(const Expr &expr) {
  if (dynamic_cast<const ComparisonExpr *>(&expr) ||
      dynamic_cast<const AndExpr *>(&expr) ||
      dynamic_cast<const OrExpr *>(&expr) ||
      dynamic_cast<const NotExpr *>(&expr)) {
    return ExprStaticType::Bool;
  }

  if (const auto *literal = dynamic_cast<const LiteralExpr *>(&expr)) {
    if (literal->value.isBool()) {
      return ExprStaticType::Bool;
    }
    if (literal->value.isNumber()) {
      return ExprStaticType::Number;
    }
    if (literal->value.isText()) {
      return ExprStaticType::Text;
    }
    return ExprStaticType::Invalid;
  }

  if (const auto *ifExpr = dynamic_cast<const IfExpr *>(&expr)) {
    if (inferExprStaticType(*ifExpr->condition) != ExprStaticType::Bool) {
      return ExprStaticType::Invalid;
    }

    const ExprStaticType thenType = inferExprStaticType(*ifExpr->thenExpr);
    const ExprStaticType elseType = inferExprStaticType(*ifExpr->elseExpr);
    if (thenType == ExprStaticType::Invalid ||
        elseType == ExprStaticType::Invalid || thenType != elseType) {
      return ExprStaticType::Invalid;
    }
    return thenType;
  }

  return ExprStaticType::Invalid;
}
