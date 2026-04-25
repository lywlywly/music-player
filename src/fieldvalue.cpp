#include "fieldvalue.h"
#include "utils.h"
#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QString>
#include <QTimeZone>

namespace {
int64_t toEpochMsUtc(const QDate &date) {
  return QDateTime(date, QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch();
}

bool parseNumber(const std::string &text, double &out) {
  bool ok = false;
  out = QString::fromStdString(text).toDouble(&ok);
  return ok;
}

bool parseDateTime(const std::string &text, int64_t &out) {
  const QString qValue = QString::fromStdString(text).trimmed();

  static const QRegularExpression ymdPattern(
      R"(^(\d{4})[-./](\d{1,2})[-./](\d{1,2})$)");
  const QRegularExpressionMatch ymdMatch = ymdPattern.match(qValue);
  if (ymdMatch.hasMatch()) {
    const int year = ymdMatch.captured(1).toInt();
    const int month = ymdMatch.captured(2).toInt();
    const int day = ymdMatch.captured(3).toInt();
    const QDate date(year, month, day);
    if (date.isValid()) {
      out = toEpochMsUtc(date);
      return true;
    }
  }

  static const QRegularExpression ymPattern(R"(^(\d{4})[-./](\d{1,2})$)");
  const QRegularExpressionMatch ymMatch = ymPattern.match(qValue);
  if (ymMatch.hasMatch()) {
    const int year = ymMatch.captured(1).toInt();
    const int month = ymMatch.captured(2).toInt();
    const QDate date(year, month, 1);
    if (date.isValid()) {
      out = toEpochMsUtc(date);
      return true;
    }
  }

  static const QRegularExpression yearPattern(R"(^(\d{4})$)");
  const QRegularExpressionMatch yearMatch = yearPattern.match(qValue);
  if (yearMatch.hasMatch()) {
    const int year = yearMatch.captured(1).toInt();
    const QDate date(year, 1, 1);
    if (date.isValid()) {
      out = toEpochMsUtc(date);
      return true;
    }
  }

  const QDate yearOnly = QDate::fromString(qValue, "yyyy");
  if (yearOnly.isValid()) {
    out = toEpochMsUtc(yearOnly);
    return true;
  }

  QDateTime dt = QDateTime::fromString(qValue, Qt::ISODateWithMs);
  if (!dt.isValid()) {
    dt = QDateTime::fromString(qValue, Qt::ISODate);
  }
  if (dt.isValid()) {
    out = dt.toMSecsSinceEpoch();
    return true;
  }

  return false;
}

bool parseBoolean(const std::string &text, bool &out) {
  const std::string normalized = util::normalizedText(text);
  if (normalized == "yes" || normalized == "true" || normalized == "1") {
    out = true;
    return true;
  }
  if (normalized == "no" || normalized == "false" || normalized == "0") {
    out = false;
    return true;
  }
  return false;
}
} // namespace

FieldValue::FieldValue(const std::string &textValue,
                       ColumnValueType valueType) {
  assign(textValue, valueType);
}

FieldValue::FieldValue(const char *textValue) {
  assign(textValue ? std::string(textValue) : std::string{},
         ColumnValueType::Text);
}

FieldValue &FieldValue::operator=(const std::string &textValue) {
  assign(textValue, ColumnValueType::Text);
  return *this;
}

FieldValue &FieldValue::operator=(const char *textValue) {
  assign(textValue ? std::string(textValue) : std::string{},
         ColumnValueType::Text);
  return *this;
}

void FieldValue::assign(const std::string &textValue,
                        ColumnValueType valueType) {
  type = valueType;
  text = textValue;
  typed.epochMs = 0;

  switch (type) {
  case ColumnValueType::Number: {
    double parsed = 0.0;
    if (parseNumber(text, parsed)) {
      typed.number = parsed;
    }
    break;
  }
  case ColumnValueType::DateTime: {
    int64_t parsed = 0;
    if (parseDateTime(text, parsed)) {
      typed.epochMs = parsed;
    }
    break;
  }
  case ColumnValueType::Boolean: {
    bool parsed = false;
    if (parseBoolean(text, parsed)) {
      typed.boolean = parsed;
      text = parsed ? "YES" : "NO";
    }
    break;
  }
  case ColumnValueType::Text:
  default:
    break;
  }
}

FieldValue FieldValue::fromDefinition(const ColumnDefinition *definition,
                                      const std::string &textValue) {
  const ColumnValueType type =
      definition ? definition->valueType : ColumnValueType::Text;
  return FieldValue(textValue, type);
}

bool FieldValue::canConvert(const std::string &textValue,
                            ColumnValueType valueType) {
  switch (valueType) {
  case ColumnValueType::Number: {
    double parsed = 0.0;
    return parseNumber(textValue, parsed);
  }
  case ColumnValueType::DateTime: {
    int64_t parsed = 0;
    return parseDateTime(textValue, parsed);
  }
  case ColumnValueType::Boolean: {
    bool parsed = false;
    return parseBoolean(textValue, parsed);
  }
  case ColumnValueType::Text:
  default:
    return true;
  }
}

bool FieldValue::operator==(const FieldValue &other) const {
  if (type != other.type || text != other.text) {
    return false;
  }
  switch (type) {
  case ColumnValueType::Number:
    return typed.number == other.typed.number;
  case ColumnValueType::DateTime:
    return typed.epochMs == other.typed.epochMs;
  case ColumnValueType::Boolean:
    return typed.boolean == other.typed.boolean;
  case ColumnValueType::Text:
  default:
    return true;
  }
}
