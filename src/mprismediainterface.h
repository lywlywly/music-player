#ifndef MPRISMEDIAINTERFACE_H
#define MPRISMEDIAINTERFACE_H

#include "isystemmediainterface.h"
#include <QDBusAbstractAdaptor>
#include <QVariantMap>

struct NowPlayingState {
  QString title;
  QString artist;
  qint64 durationMs = 0;
  qint64 positionMs = 0;
  bool isPlaying = false;
};

class MprisMediaInterface;

// Root adaptor: org.mpris.MediaPlayer2
class MprisRootAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")

  Q_PROPERTY(bool CanQuit READ canQuit)
  Q_PROPERTY(bool CanRaise READ canRaise)
  Q_PROPERTY(bool HasTrackList READ hasTrackList)
  Q_PROPERTY(QString Identity READ identity)
  Q_PROPERTY(QString DesktopEntry READ desktopEntry)

public:
  explicit MprisRootAdaptor(MprisMediaInterface *parent);

  bool canQuit() const { return false; }
  bool canRaise() const { return false; }
  bool hasTrackList() const { return false; }

  // Name shown to controllers
  QString identity() const { return QStringLiteral("myplayer"); }
  // Used by some desktops to link desktop file
  QString desktopEntry() const { return QStringLiteral("myplayer"); }

public slots:
  void Raise() {}
  void Quit() {}
};

// Player adaptor: org.mpris.MediaPlayer2.Player
class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
  Q_PROPERTY(bool CanControl READ canControl)
  Q_PROPERTY(bool CanPlay READ canPlay)
  Q_PROPERTY(bool CanPause READ canPause)
  Q_PROPERTY(bool CanGoNext READ canGoNext)
  Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
public:
  explicit MprisPlayerAdaptor(MprisMediaInterface *parent);
  bool canControl() const { return true; }
  bool canPlay() const { return true; }
  bool canPause() const { return true; }
  bool canGoNext() const { return true; }
  bool canGoPrevious() const { return true; }
public slots:
  void Play();
  void Pause();
  void PlayPause();
  void Next();
  void Previous();

private:
  MprisMediaInterface *backend_;
};

class MprisMediaInterface : public ISystemMediaInterface {
  Q_OBJECT
public:
  explicit MprisMediaInterface(QObject *parent = nullptr);
  ~MprisMediaInterface() override = default;
  void updateNowPlaying(const QString &title, const QString &artist,
                        qint64 durationMs, qint64 positionMs,
                        bool isPlaying) override;
  void setTrackInfo(const QString &title, const QString &artist,
                    qint64 durationMs) override;
  void updatePosition(qint64 positionMs) override;
  void updatePlaybackState(bool isPlaying) override;
  // Called by adaptor
  void emitPlayRequested() { emit playRequested(); }
  void emitPauseRequested() { emit pauseRequested(); }
  void emitToggleRequested() { emit toggleRequested(); }
  void emitNextRequested() { emit nextRequested(); }
  void emitPreviousRequested() { emit previousRequested(); }

private:
  void sendPropertiesChanged();
  QVariantMap buildMetadata() const;

  NowPlayingState state_;
  MprisRootAdaptor *rootAdaptor_ = nullptr;
  MprisPlayerAdaptor *playerAdaptor_ = nullptr;
};

#endif // MPRISMEDIAINTERFACE_H
