#include "fieldvalue.h"
#include "fieldtypepool.h"
#include "utils.h"
#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QString>
#include <QTimeZone>
#include <utility>

namespace {
int64_t toEpochMsUtc(const QDate &date) {
  return QDateTime(date, QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch();
}

QString formatDisplayByKind(const std::string &text, DisplayKind displayKind) {
  const QString raw = QString::fromStdString(text);
  switch (displayKind) {
  case DisplayKind::EpochSecondsDateTime: {
    bool ok = false;
    const qint64 epochSeconds = raw.toLongLong(&ok);
    if (!ok || epochSeconds <= 0) {
      return raw;
    }
    return QDateTime::fromSecsSinceEpoch(epochSeconds)
        .toLocalTime()
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
  }
  case DisplayKind::DurationSeconds: {
    bool ok = false;
    const qint64 totalSeconds = raw.toLongLong(&ok);
    if (!ok || totalSeconds < 0) {
      return raw;
    }
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
      return QStringLiteral("%1:%2:%3")
          .arg(hours)
          .arg(minutes, 2, 10, QChar('0'))
          .arg(seconds, 2, 10, QChar('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
  }
  case DisplayKind::ChannelLayout: {
    bool ok = false;
    const qint64 channelCount = raw.toLongLong(&ok);
    if (!ok) {
      return raw;
    }
    if (channelCount == 1) {
      return QStringLiteral("mono");
    }
    if (channelCount == 2) {
      return QStringLiteral("stereo");
    }
    return raw;
  }
  case DisplayKind::Raw:
  default:
    return raw;
  }
}
} // namespace

FieldValue::FieldValue(const std::string &textValue, std::string fieldIdValue) {
  assign(textValue, std::move(fieldIdValue));
}

void FieldValue::assign(const std::string &textValue,
                        std::string fieldIdValue) {
  if (fieldIdValue.empty()) {
    qFatal("FieldValue::assign requires non-empty fieldId");
  }
  text = textValue;
  fieldId = std::move(fieldIdValue);
  typed.numberInt = 0;

  const FieldDefinition *definition = FieldTypePool::instance().find(fieldId);
  if (!definition) {
    return;
  }

  switch (definition->valueType) {
  case ValueType::Number: {
    typed.numberDouble = 0.0;
    double parsed = 0.0;
    if (parseNumber(text, parsed)) {
      typed.numberDouble = parsed;
    }
    return;
  }
  case ValueType::DateTime: {
    typed.numberInt = 0;
    int64_t parsed = 0;
    if (parseDateTimeEpochMs(text, parsed)) {
      typed.numberInt = parsed;
    }
    return;
  }
  case ValueType::Boolean: {
    typed.boolean = false;
    bool parsed = false;
    if (parseBoolean(text, parsed)) {
      typed.boolean = parsed;
    }
    return;
  }
  case ValueType::Text:
  default:
    return;
  }
}

ValueType FieldValue::valueType() const {
  const FieldDefinition *definition = FieldTypePool::instance().find(fieldId);
  if (!definition) {
    qFatal("FieldValue::valueType missing FieldDefinition for fieldId=%s",
           fieldId.c_str());
  }
  return definition->valueType;
}

bool FieldValue::parseNumber(const std::string &textValue, double &out) {
  bool ok = false;
  out = QString::fromStdString(textValue).toDouble(&ok);
  return ok;
}

bool FieldValue::parseDateTimeEpochMs(const std::string &textValue,
                                      int64_t &out) {
  const QString qValue = QString::fromStdString(textValue).trimmed();
  if (qValue.isEmpty()) {
    return false;
  }

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

  bool numericOk = false;
  const qint64 numericValue = qValue.toLongLong(&numericOk);
  if (numericOk) {
    constexpr qint64 kLikelyEpochMsThreshold = 1'000'000'000'000LL;
    if (numericValue > -kLikelyEpochMsThreshold &&
        numericValue < kLikelyEpochMsThreshold) {
      out = numericValue * 1000;
    } else {
      out = numericValue;
    }
    return true;
  }

  QDateTime dt =
      QDateTime::fromString(qValue, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
  if (dt.isValid()) {
    out = dt.toMSecsSinceEpoch();
    return true;
  }

  dt = QDateTime::fromString(qValue, Qt::ISODateWithMs);
  if (!dt.isValid()) {
    dt = QDateTime::fromString(qValue, Qt::ISODate);
  }
  if (dt.isValid()) {
    out = dt.toMSecsSinceEpoch();
    return true;
  }

  return false;
}

bool FieldValue::parseBoolean(const std::string &textValue, bool &out) {
  const std::string normalized = util::normalizedText(textValue);
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

bool FieldValue::canConvert(const std::string &textValue, ValueType valueType) {
  switch (valueType) {
  case ValueType::Number: {
    double parsed = 0.0;
    return parseNumber(textValue, parsed);
  }
  case ValueType::DateTime: {
    int64_t parsed = 0;
    return parseDateTimeEpochMs(textValue, parsed);
  }
  case ValueType::Boolean: {
    bool parsed = false;
    return parseBoolean(textValue, parsed);
  }
  case ValueType::Text:
  default:
    return true;
  }
}

QString FieldValue::display() const {
  const FieldDefinition *definition = FieldTypePool::instance().find(fieldId);
  if (!definition) {
    return QString::fromStdString(text);
  }
  return formatDisplayByKind(text, definition->displayKind);
}

bool FieldValue::operator==(const FieldValue &other) const {
  return text == other.text && fieldId == other.fieldId;
}
