#include "libraryexpression_ops.h"
#include "fieldformatter.h"
#include "utils.h"

#include <QStringList>

namespace {
int compareFieldValue(std::string_view fieldText, ValueType valueType,
                      std::string_view exprValue);
std::string runtimeValueToText(const ExprRuntimeValue &runtimeValue);

QStringList splitMultiValueText(const std::string &text) {
  QString normalized = QString::fromStdString(text);
  normalized.replace(QStringLiteral(" / "), QStringLiteral(","));
  return normalized.split(QStringLiteral(","), Qt::SkipEmptyParts);
}

bool fieldHasValue(const std::string &fieldText, const std::string &exprValue) {
  const QStringList parts = splitMultiValueText(fieldText);
  const QString normalizedExpression =
      util::normalizedText(QString::fromStdString(exprValue));
  for (const QString &part : parts) {
    if (util::normalizedText(part) == normalizedExpression) {
      return true;
    }
  }
  return false;
}

bool fieldHasTypedValue(std::string_view fieldText, ValueType valueType,
                        const std::string &exprValue) {
  if (fieldText.empty()) {
    return false;
  }

  const QStringList parts = splitMultiValueText(std::string(fieldText));
  for (const QString &part : parts) {
    const std::string partText = part.trimmed().toStdString();
    if (partText.empty()) {
      continue;
    }
    bool ok = false;
    const int cmp = compareFieldText(partText, exprValue, valueType, ok);
    if (ok && cmp == 0) {
      return true;
    }
  }
  return false;
}

int compareFieldValue(std::string_view fieldText, ValueType valueType,
                      std::string_view exprValue) {
  bool ok = false;
  const int result = compareFieldText(std::string(fieldText),
                                      std::string(exprValue), valueType, ok);
  if (ok) {
    return result;
  }

  const std::string normalizedField =
      util::normalizedText(std::string(fieldText));
  const std::string normalizedExpression =
      util::normalizedText(std::string(exprValue));
  if (normalizedField < normalizedExpression) {
    return -1;
  }
  if (normalizedField > normalizedExpression) {
    return 1;
  }
  return 0;
}

ValueType runtimeValueType(const ExprRuntimeValue &runtimeValue) {
  if (runtimeValue.isBool()) {
    return ValueType::Boolean;
  }
  if (runtimeValue.isNumber()) {
    return ValueType::Number;
  }
  return ValueType::Text;
}

std::string runtimeValueToText(const ExprRuntimeValue &runtimeValue) {
  return runtimeValueToQString(runtimeValue).toStdString();
}
} // namespace

QString runtimeValueToQString(const ExprRuntimeValue &runtimeValue) {
  if (runtimeValue.isText()) {
    return QString::fromStdString(runtimeValue.textValue());
  }
  if (runtimeValue.isNumber()) {
    return QString::number(runtimeValue.numberValue(), 'g', 17);
  }
  return runtimeValue.boolValueOrFalse() ? QStringLiteral("true")
                                         : QStringLiteral("false");
}

std::string
Expr::evaluateDisplayText(const LibraryExprEvalContext &context) const {
  return runtimeValueToText(evaluateValue(context));
}

bool exprValueMatchesFieldType(const ExprValue &value, ValueType valueType) {
  for (const std::string &item : value.values) {
    if (!FieldValue::canConvert(item, valueType)) {
      return false;
    }
  }
  return true;
}

bool supportsRangeValueType(ValueType valueType) {
  return valueType == ValueType::Number || valueType == ValueType::DateTime;
}

bool isRangeBoundaryOrderValid(const ExprValue &value, ValueType valueType) {
  if (!value.isRange()) {
    return true;
  }
  return compareFieldValue(value.rangeStart(), valueType, value.rangeEnd()) <=
         0;
}

bool IsOperator::evaluate(std::string_view fieldText, ValueType valueType,
                          const ExprValue &exprValue) const {
  if (valueType != ValueType::Text && fieldText.empty()) {
    return false;
  }
  return compareFieldValue(fieldText, valueType, exprValue.scalarText()) == 0;
}

bool IsOperator::equals(const ExprOperator &other) const {
  return dynamic_cast<const IsOperator *>(&other) != nullptr;
}

bool IsOperator::supportsValue(const ExprValue &exprValue) const {
  return exprValue.isScalar();
}

std::string IsOperator::displayName() const { return "IS"; }

bool HasOperator::evaluate(std::string_view fieldText, ValueType valueType,
                           const ExprValue &exprValue) const {
  if (valueType != ValueType::Text) {
    return fieldHasTypedValue(fieldText, valueType, exprValue.scalarText());
  }
  return fieldHasValue(std::string(fieldText), exprValue.scalarText());
}

bool HasOperator::equals(const ExprOperator &other) const {
  return dynamic_cast<const HasOperator *>(&other) != nullptr;
}

bool HasOperator::supportsValue(const ExprValue &exprValue) const {
  return exprValue.isScalar();
}

std::string HasOperator::displayName() const { return "HAS"; }

bool InOperator::evaluate(std::string_view fieldText, ValueType valueType,
                          const ExprValue &exprValue) const {
  if (exprValue.isRange()) {
    if (fieldText.empty()) {
      return false;
    }
    return compareFieldValue(fieldText, valueType, exprValue.rangeStart()) >=
               0 &&
           compareFieldValue(fieldText, valueType, exprValue.rangeEnd()) <= 0;
  }

  const std::string normalizedField =
      util::normalizedText(std::string(fieldText));
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

bool LtOperator::evaluate(std::string_view fieldText, ValueType valueType,
                          const ExprValue &exprValue) const {
  return !fieldText.empty() &&
         compareFieldValue(fieldText, valueType, exprValue.scalarText()) < 0;
}

bool LtOperator::equals(const ExprOperator &other) const {
  return dynamic_cast<const LtOperator *>(&other) != nullptr;
}

bool LtOperator::supportsValue(const ExprValue &exprValue) const {
  return exprValue.isScalar();
}

std::string LtOperator::displayName() const { return "<"; }

bool LteOperator::evaluate(std::string_view fieldText, ValueType valueType,
                           const ExprValue &exprValue) const {
  return !fieldText.empty() &&
         compareFieldValue(fieldText, valueType, exprValue.scalarText()) <= 0;
}

bool LteOperator::equals(const ExprOperator &other) const {
  return dynamic_cast<const LteOperator *>(&other) != nullptr;
}

bool LteOperator::supportsValue(const ExprValue &exprValue) const {
  return exprValue.isScalar();
}

std::string LteOperator::displayName() const { return "<="; }

bool GtOperator::evaluate(std::string_view fieldText, ValueType valueType,
                          const ExprValue &exprValue) const {
  return !fieldText.empty() &&
         compareFieldValue(fieldText, valueType, exprValue.scalarText()) > 0;
}

bool GtOperator::equals(const ExprOperator &other) const {
  return dynamic_cast<const GtOperator *>(&other) != nullptr;
}

bool GtOperator::supportsValue(const ExprValue &exprValue) const {
  return exprValue.isScalar();
}

std::string GtOperator::displayName() const { return ">"; }

bool GteOperator::evaluate(std::string_view fieldText, ValueType valueType,
                           const ExprValue &exprValue) const {
  return !fieldText.empty() &&
         compareFieldValue(fieldText, valueType, exprValue.scalarText()) >= 0;
}

bool GteOperator::equals(const ExprOperator &other) const {
  return dynamic_cast<const GteOperator *>(&other) != nullptr;
}

bool GteOperator::supportsValue(const ExprValue &exprValue) const {
  return exprValue.isScalar();
}

std::string GteOperator::displayName() const { return ">="; }

ComparisonExpr::ComparisonExpr(ExprPtr leftExpression, ExprOperatorPtr exprOp,
                               ExprPtr rightExpression, ValueType leftValueType)
    : leftExpr(std::move(leftExpression)),
      rightExpr(std::move(rightExpression)), op(std::move(exprOp)),
      valueType(leftValueType) {}

ExprRuntimeValue
ComparisonExpr::evaluateValue(const LibraryExprEvalContext &context) const {
  const ExprRuntimeValue leftValue = leftExpr->evaluateValue(context);
  const auto *valueExpr = dynamic_cast<const ExprValueExpr *>(rightExpr.get());
  if (valueExpr) {
    return ExprRuntimeValue::fromBool(op->evaluate(
        runtimeValueToText(leftValue), valueType, valueExpr->value));
  }

  const ExprRuntimeValue rightValue = rightExpr->evaluateValue(context);
  ExprValue scalarValue;
  scalarValue.kind = ExprValue::Kind::Scalar;
  scalarValue.values = {runtimeValueToText(rightValue)};
  return ExprRuntimeValue::fromBool(
      op->evaluate(runtimeValueToText(leftValue), valueType, scalarValue));
}

bool ComparisonExpr::equals(const Expr &other) const {
  auto comparison = dynamic_cast<const ComparisonExpr *>(&other);
  if (!comparison) {
    return false;
  }
  if (valueType != comparison->valueType ||
      !leftExpr->equals(*comparison->leftExpr) ||
      !rightExpr->equals(*comparison->rightExpr)) {
    return false;
  }
  return *op == *comparison->op;
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

FieldRefExpr::FieldRefExpr(ExprFieldRef fieldRef)
    : field(std::move(fieldRef)) {}

ExprRuntimeValue
FieldRefExpr::evaluateValue(const LibraryExprEvalContext &context) const {
  const FieldValue *fieldValue = context.fieldValue(field.resolvedColumnId);
  if (!fieldValue || fieldValue->text.empty()) {
    return ExprRuntimeValue::fromText({});
  }
  if (field.valueType == ValueType::Number) {
    return ExprRuntimeValue::fromNumber(fieldValue->typed.numberDouble);
  }
  if (field.valueType == ValueType::Boolean) {
    return ExprRuntimeValue::fromBool(fieldValue->typed.boolean);
  }
  return ExprRuntimeValue::fromText(fieldValue->text);
}

std::string
FieldRefExpr::evaluateDisplayText(const LibraryExprEvalContext &context) const {
  const FieldValue *fieldValue = context.fieldValue(field.resolvedColumnId);
  if (!fieldValue) {
    return {};
  }
  return fieldValue->display().toStdString();
}

bool FieldRefExpr::equals(const Expr &other) const {
  const auto *fieldExpr = dynamic_cast<const FieldRefExpr *>(&other);
  if (!fieldExpr) {
    return false;
  }
  return field.exprFieldName == fieldExpr->field.exprFieldName &&
         field.resolvedColumnId == fieldExpr->field.resolvedColumnId &&
         field.valueType == fieldExpr->field.valueType;
}

InterpolatedStringExpr::InterpolatedStringExpr(std::vector<ExprPtr> exprParts)
    : parts(std::move(exprParts)) {}

ExprRuntimeValue InterpolatedStringExpr::evaluateValue(
    const LibraryExprEvalContext &context) const {
  std::string out;
  for (const ExprPtr &part : parts) {
    out += part->evaluateDisplayText(context);
  }
  return ExprRuntimeValue::fromText(std::move(out));
}

bool InterpolatedStringExpr::equals(const Expr &other) const {
  const auto *interpolated =
      dynamic_cast<const InterpolatedStringExpr *>(&other);
  if (!interpolated || parts.size() != interpolated->parts.size()) {
    return false;
  }
  for (size_t i = 0; i < parts.size(); ++i) {
    const ExprPtr &left = parts[i];
    const ExprPtr &right = interpolated->parts[i];
    if (static_cast<bool>(left) != static_cast<bool>(right)) {
      return false;
    }
    if (left && !left->equals(*right)) {
      return false;
    }
  }
  return true;
}

ExprValueExpr::ExprValueExpr(ExprValue exprValue)
    : value(std::move(exprValue)) {}

ExprRuntimeValue
ExprValueExpr::evaluateValue(const LibraryExprEvalContext &) const {
  if (value.kind == ExprValue::Kind::Scalar && !value.values.empty()) {
    return ExprRuntimeValue::fromText(value.values.front());
  }
  if (value.kind == ExprValue::Kind::Range && value.values.size() == 2) {
    return ExprRuntimeValue::fromText(value.values.front() + ".." +
                                      value.values.back());
  }
  return ExprRuntimeValue::fromText({});
}

bool ExprValueExpr::equals(const Expr &other) const {
  const auto *valueExpr = dynamic_cast<const ExprValueExpr *>(&other);
  if (!valueExpr) {
    return false;
  }
  return value.kind == valueExpr->value.kind &&
         value.values == valueExpr->value.values;
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

  if (const auto *fieldExpr = dynamic_cast<const FieldRefExpr *>(&expr)) {
    if (fieldExpr->field.valueType == ValueType::Boolean) {
      return ExprStaticType::Bool;
    }
    if (fieldExpr->field.valueType == ValueType::Number) {
      return ExprStaticType::Number;
    }
    return ExprStaticType::Text;
  }

  if (dynamic_cast<const InterpolatedStringExpr *>(&expr)) {
    return ExprStaticType::Text;
  }

  return ExprStaticType::Invalid;
}
