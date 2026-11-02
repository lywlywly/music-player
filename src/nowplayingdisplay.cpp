#include "nowplayingdisplay.h"

#include "songparser.h"
#include <QDebug>
#include <algorithm>

NowPlayingDisplay::NowPlayingDisplay(const ColumnRegistry &registry)
    : registry_(registry) {
  loadExpressions();
}

void NowPlayingDisplay::loadExpressions() {
  for (DisplayExpressionKind kind :
       {DisplayExpressionKind::StatusBar, DisplayExpressionKind::WindowTitle}) {
    setExpression(kind, loadDisplayExpressionSetting(kind));
  }
}

void NowPlayingDisplay::setExpression(DisplayExpressionKind kind,
                                      const QString &expression) {
  ExprParseResult parsed = parseDisplayExpression(
      effectiveDisplayExpression(kind, expression), registry_);
  if (!parsed.ok()) {
    qWarning() << "NowPlayingDisplay::setExpression: parse failed:"
               << parsed.error.message << "at" << parsed.error.position;
    parsed = parseDisplayExpression(defaultDisplayExpression(kind), registry_);
    if (!parsed.ok()) {
      qFatal("NowPlayingDisplay::setExpression: failed to parse default "
             "expression");
    }
  }
  ExprPtr &target = kind == DisplayExpressionKind::StatusBar ? statusBarExpr_
                                                             : windowTitleExpr_;
  target = std::move(parsed.expr);
}

void NowPlayingDisplay::startSong(const MSong &song) {
  stopSong(); // drop the previous song's bitrates
  if (const auto it = song.find("bitrate"); it != song.end()) {
    const FieldValue &bitrate = it->second;
    if (bitrate.valueType() == ValueType::Number) {
      tagBitrateBps_ = static_cast<qint64>(bitrate.typed.numberDouble *
                                           static_cast<double>(1000));
    }
  }
  if (tagBitrateBps_ > 0) {
    preferTagBitrate_ =
        SongParser::isLikelyCbrAudioFile(song.at("filepath").text);
  }
  updateBitrateSymbol();
}

void NowPlayingDisplay::stopSong() {
  streamBitrateBps_ = 0;
  tagBitrateBps_ = 0;
  preferTagBitrate_ = false;
  updateBitrateSymbol();
}

void NowPlayingDisplay::setPositionMs(qint64 positionMs) {
  runtimeSymbols_.setPlaybackTimeSeconds(positionMs / 1000);
}

void NowPlayingDisplay::setDurationMs(qint64 durationMs) {
  runtimeSymbols_.setDurationSeconds(durationMs / 1000);
}

bool NowPlayingDisplay::setStreamBitrateBps(qint64 bitsPerSecond) {
  if (preferTagBitrate_) {
    return false;
  }
  streamBitrateBps_ = std::max<qint64>(0, bitsPerSecond);
  updateBitrateSymbol();
  return true;
}

void NowPlayingDisplay::updateBitrateSymbol() {
  // With preferTagBitrate_ the stream bitrate stays 0, so the tag one is used.
  const qint64 streamKbps = streamBitrateBps_ / static_cast<qint64>(1000);
  runtimeSymbols_.setBitrateKbps(
      streamKbps > 0 ? streamKbps : tagBitrateBps_ / static_cast<qint64>(1000));
}

const StatusRuntimeSymbolTable &
NowPlayingDisplay::runtimeSymbols(PlaybackQueue::PlaybackStatus status) const {
  runtimeSymbols_.setIsPlaying(status ==
                               PlaybackQueue::PlaybackStatus::Playing);
  runtimeSymbols_.setIsPaused(status == PlaybackQueue::PlaybackStatus::Paused);
  return runtimeSymbols_;
}

QString NowPlayingDisplay::evaluate(const ExprPtr &expr,
                                    PlaybackQueue::PlaybackStatus status,
                                    const MSong *song) const {
  return evaluateDisplayExpression(*expr, runtimeSymbols(status), song);
}

QString NowPlayingDisplay::statusBarText(PlaybackQueue::PlaybackStatus status,
                                         const MSong *song) const {
  if (status == PlaybackQueue::PlaybackStatus::None) {
    return {};
  }
  return evaluate(statusBarExpr_, status, song);
}

QString NowPlayingDisplay::windowTitle(PlaybackQueue::PlaybackStatus status,
                                       const MSong *song,
                                       const QString &idleTitle) const {
  if (status == PlaybackQueue::PlaybackStatus::None) {
    return idleTitle;
  }
  return evaluate(windowTitleExpr_, status, song);
}
