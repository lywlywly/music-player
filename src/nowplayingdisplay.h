#ifndef NOWPLAYINGDISPLAY_H
#define NOWPLAYINGDISPLAY_H

#include "displayexpression.h"
#include "libraryexpression.h"
#include "playbackqueue.h"
#include "songlibrary.h"
#include "statusruntimesymboltable.h"
#include <QString>

class ColumnRegistry;

// Builds the status bar text and window title from the user's display
// expressions, the playback state and the current song.
//
// Owns the parsed expressions and the playback values shown through the
// `status:*` symbols: position, duration and the bitrate chosen for the
// current song. Playback status and the current song are passed in by the
// caller, which owns them. Has no widgets; MainWindow applies the strings.
class NowPlayingDisplay {
public:
  // Loads both expressions from settings.
  explicit NowPlayingDisplay(const ColumnRegistry &registry);

  // Uses expression for this kind (a blank one means the default). An
  // expression that does not parse falls back to the default one.
  void setExpression(DisplayExpressionKind kind, const QString &expression);

  // A song started: takes its tag bitrate, and decides whether that or the
  // backend's stream bitrate is shown.
  void startSong(const MSong &song);
  // Playback stopped: forgets the song's bitrates.
  void stopSong();
  void setPositionMs(qint64 positionMs);
  void setDurationMs(qint64 durationMs);
  // Returns false when the value is ignored because the tag bitrate of a
  // constant-bitrate file is shown instead.
  bool setStreamBitrateBps(qint64 bitsPerSecond);

  // The `status:*` values for this playback status. The reference stays valid
  // until the next call on this object.
  const StatusRuntimeSymbolTable &
  runtimeSymbols(PlaybackQueue::PlaybackStatus status) const;

  // Empty when nothing is playing. song may be nullptr.
  QString statusBarText(PlaybackQueue::PlaybackStatus status,
                        const MSong *song) const;
  // idleTitle when nothing is playing. song may be nullptr.
  QString windowTitle(PlaybackQueue::PlaybackStatus status, const MSong *song,
                      const QString &idleTitle) const;

private:
  QString evaluate(const ExprPtr &expr, PlaybackQueue::PlaybackStatus status,
                   const MSong *song) const;
  void updateBitrateSymbol();

  const ColumnRegistry &registry_;
  ExprPtr statusBarExpr_;
  ExprPtr windowTitleExpr_;
  // Kept up to date by the setters; runtimeSymbols() only sets the status
  // flags. One instance, because constructing one registers the runtime field
  // types in FieldTypePool.
  mutable StatusRuntimeSymbolTable runtimeSymbols_;
  qint64 streamBitrateBps_ = 0;
  qint64 tagBitrateBps_ = 0;
  // A constant-bitrate file with a tag bitrate (> 0) shows that rather than
  // the stream's; stream reports are then ignored.
  bool preferTagBitrate_ = false;
};

#endif // NOWPLAYINGDISPLAY_H
