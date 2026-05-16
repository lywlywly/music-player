#include "fieldformatter.h"
#include "utils.h"

namespace {
template <typename T> int compareOrdered(const T &left, const T &right) {
  if (left < right) {
    return -1;
  }
  if (left > right) {
    return 1;
  }
  return 0;
}
} // namespace

int compareFieldText(const std::string &left, const std::string &right,
                     ValueType type, bool &ok) {
  ok = true;
  switch (type) {
  case ValueType::Number: {
    double leftN = 0.0;
    double rightN = 0.0;
    if (!FieldValue::parseNumber(left, leftN) ||
        !FieldValue::parseNumber(right, rightN)) {
      ok = false;
      return 0;
    }
    return compareOrdered(leftN, rightN);
  }
  case ValueType::DateTime: {
    int64_t leftMs = 0;
    int64_t rightMs = 0;
    if (!FieldValue::parseDateTimeEpochMs(left, leftMs) ||
        !FieldValue::parseDateTimeEpochMs(right, rightMs)) {
      ok = false;
      return 0;
    }
    return compareOrdered(leftMs, rightMs);
  }
  case ValueType::Boolean: {
    bool leftB = false;
    bool rightB = false;
    if (!FieldValue::parseBoolean(left, leftB) ||
        !FieldValue::parseBoolean(right, rightB)) {
      ok = false;
      return 0;
    }
    if (leftB == rightB) {
      return 0;
    }
    return leftB ? 1 : -1;
  }
  case ValueType::Text:
  default: {
    const std::string normalizedLeft = util::normalizedText(left);
    const std::string normalizedRight = util::normalizedText(right);
    return compareOrdered(normalizedLeft, normalizedRight);
  }
  }
}

int compareFieldValues(const FieldValue &left, const FieldValue &right,
                       ValueType type, bool &ok) {
  ok = true;
  switch (type) {
  case ValueType::Number: {
    return compareOrdered(left.typed.numberDouble, right.typed.numberDouble);
  }
  case ValueType::DateTime: {
    return compareOrdered(left.typed.numberInt, right.typed.numberInt);
  }
  case ValueType::Boolean: {
    if (left.typed.boolean == right.typed.boolean) {
      return 0;
    }
    return left.typed.boolean ? 1 : -1;
  }
  case ValueType::Text:
  default: {
    const std::string normalizedLeft = util::normalizedText(left.text);
    const std::string normalizedRight = util::normalizedText(right.text);
    return compareOrdered(normalizedLeft, normalizedRight);
  }
  }
}
