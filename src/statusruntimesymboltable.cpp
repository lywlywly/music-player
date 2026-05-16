#include "statusruntimesymboltable.h"

#include "fieldtypepool.h"
#include "utils.h"
#include <QString>

namespace {
constexpr auto kStatusPrefix = "status:";

std::string statusFieldId(std::string_view name) {
  std::string out(kStatusPrefix);
  out += name;
  return out;
}

std::string statusLocalName(const QString &fieldId) {
  return fieldId.mid(7).toStdString();
}
} // namespace

StatusRuntimeSymbolTable::StatusRuntimeSymbolTable() {
  FieldTypePool::instance().upsert(fieldDefinitions());
  values_.reserve(fieldDefinitions().size());
  for (const FieldDefinition &definition : fieldDefinitions()) {
    const std::string fieldId = definition.id.toStdString();
    values_.emplace(fieldId, FieldValue("0", fieldId));
  }
  values_.at(statusFieldId("isplaying"))
      .assign("false", statusFieldId("isplaying"));
  values_.at(statusFieldId("ispaused"))
      .assign("false", statusFieldId("ispaused"));
}

const QString &StatusRuntimeSymbolTable::defaultStatusBarExpression() {
  static const QString kDefaultStatusBarExpression = QStringLiteral(
      "`"
      "${codec} | ${bitrate} kbps | ${sample_rate} Hz | ${channels} | "
      "${playback_time}${IF duration > 0 THEN ` / ${duration}` ELSE ''} | "
      "${IF ispaused IS true THEN 'Paused' ELSE IF isplaying IS true THEN "
      "'Playing' ELSE 'Stopped'}`");
  return kDefaultStatusBarExpression;
}

const QString &StatusRuntimeSymbolTable::defaultWindowTitleExpression() {
  static const QString kDefaultWindowTitleExpression = QStringLiteral(
      "`${IF artist IS '' THEN title ELSE `${artist} - ${title}`}`");
  return kDefaultWindowTitleExpression;
}

const std::vector<FieldDefinition> &
StatusRuntimeSymbolTable::fieldDefinitions() {
  static const std::vector<FieldDefinition> kDefinitions = {
      {.id = QStringLiteral("status:isplaying"),
       .valueType = ValueType::Boolean},
      {.id = QStringLiteral("status:ispaused"),
       .valueType = ValueType::Boolean},
      {.id = QStringLiteral("status:playback_time"),
       .valueType = ValueType::Number,
       .displayKind = DisplayKind::DurationSeconds},
      {.id = QStringLiteral("status:duration"),
       .valueType = ValueType::Number,
       .displayKind = DisplayKind::DurationSeconds},
      {.id = QStringLiteral("status:bitrate"), .valueType = ValueType::Number},
  };
  return kDefinitions;
}

const std::vector<ExprSymbolInfo> &
StatusRuntimeSymbolTable::expressionSymbols() {
  static const std::vector<ExprSymbolInfo> kRuntimeDefinitions = [] {
    std::vector<ExprSymbolInfo> out;
    out.reserve(fieldDefinitions().size() * 2);
    for (const FieldDefinition &definition : fieldDefinitions()) {
      const std::string resolvedId =
          util::normalizedText(definition.id).toStdString();
      const std::string localName =
          util::normalizedText(statusLocalName(definition.id));
      out.push_back({.name = localName,
                     .resolvedId = resolvedId,
                     .valueType = definition.valueType});
      out.push_back({.name = resolvedId,
                     .resolvedId = resolvedId,
                     .valueType = definition.valueType});
    }
    return out;
  }();
  return kRuntimeDefinitions;
}

void StatusRuntimeSymbolTable::setIsPlaying(bool isPlaying) {
  const std::string fieldId = statusFieldId("isplaying");
  values_.at(fieldId).assign(isPlaying ? "true" : "false", fieldId);
}

void StatusRuntimeSymbolTable::setIsPaused(bool isPaused) {
  const std::string fieldId = statusFieldId("ispaused");
  values_.at(fieldId).assign(isPaused ? "true" : "false", fieldId);
}

void StatusRuntimeSymbolTable::setPlaybackTimeSeconds(qint64 seconds) {
  const std::string fieldId = statusFieldId("playback_time");
  values_.at(fieldId).assign(
      QString::number(clampNonNegative(seconds)).toStdString(), fieldId);
}

void StatusRuntimeSymbolTable::setDurationSeconds(qint64 seconds) {
  const std::string fieldId = statusFieldId("duration");
  values_.at(fieldId).assign(
      QString::number(clampNonNegative(seconds)).toStdString(), fieldId);
}

void StatusRuntimeSymbolTable::setBitrateKbps(qint64 bitrateKbps) {
  const std::string fieldId = statusFieldId("bitrate");
  values_.at(fieldId).assign(
      QString::number(clampNonNegative(bitrateKbps)).toStdString(), fieldId);
}

const FieldValue *
StatusRuntimeSymbolTable::fieldValue(std::string_view fieldId) const {
  auto it = values_.find(std::string(fieldId));
  if (it == values_.end()) {
    return nullptr;
  }
  return &it->second;
}

qint64 StatusRuntimeSymbolTable::clampNonNegative(qint64 value) {
  return value < 0 ? 0 : value;
}
