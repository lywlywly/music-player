#ifndef NOWPLAYINGPRESENTER_H
#define NOWPLAYINGPRESENTER_H

#include "displayexpression.h"
#include "nowplayingdisplay.h"
#include <QObject>
#include <QString>

class ColumnRegistry;
class PlaybackController;
class QMainWindow;

// Shows the now-playing status bar text and window title: keeps
// NowPlayingDisplay up to date from PlaybackController's signals and applies
// its strings to the window. Frontend class.
class NowPlayingPresenter : public QObject {
  Q_OBJECT

public:
  // A child of window. The window's current title is shown when nothing is
  // playing.
  NowPlayingPresenter(const ColumnRegistry &registry,
                      PlaybackController &playback, QMainWindow &window);

  // Re-parses both expressions from settings; see
  // NowPlayingDisplay::loadExpressions.
  void reloadExpressions();
  void setExpression(DisplayExpressionKind kind, const QString &expression);

  // The `status:*` values for the current playback status. The reference stays
  // valid until the next call on this object.
  const StatusRuntimeSymbolTable &runtimeSymbols() const;

signals:
  // The status bar text and window title were updated.
  void refreshed();

private:
  void refresh();

  PlaybackController &playback_;
  QMainWindow &window_;
  NowPlayingDisplay display_;
  QString idleTitle_;
};

#endif // NOWPLAYINGPRESENTER_H
