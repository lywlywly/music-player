#ifndef MPRISMEDIAINTERFACE_H
#define MPRISMEDIAINTERFACE_H

#include "isystemmediainterface.h"
#include <QByteArray>
#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QVariantMap>
#include <atomic>

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
  Q_PROPERTY(bool CanSeek READ canSeek)
  Q_PROPERTY(qlonglong Position READ position)
public:
  explicit MprisPlayerAdaptor(MprisMediaInterface *parent);
  bool canControl() const { return true; }
  bool canPlay() const { return true; }
  bool canPause() const { return true; }
  bool canGoNext() const { return true; }
  bool canGoPrevious() const { return true; }
  bool canSeek() const { return true; }
  qlonglong position() const;
public slots:
  void Play();
  void Pause();
  void PlayPause();
  void Next();
  void Previous();
  void Seek(qlonglong offsetUs);
  void SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs);

private:
  MprisMediaInterface *backend_;
};

class MprisMediaInterface : public ISystemMediaInterface {
  Q_OBJECT
public:
  explicit MprisMediaInterface(QObject *parent = nullptr);
  ~MprisMediaInterface() override = default;
  void setTitleAndArtist(const QString &title, const QString &artist) override;
  void setPlaybackState(PlaybackState state) override;
  void setDuration(qint64 durationMs) override;
  void setPosition(qint64 positionMs) override;
  void setArtwork(const QByteArray &imageData) override;
  // Called by adaptor
  void emitPlayRequested() { emit playRequested(); }
  void emitPauseRequested() { emit pauseRequested(); }
  void emitToggleRequested() { emit toggleRequested(); }
  void emitNextRequested() { emit nextRequested(); }
  void emitPreviousRequested() { emit previousRequested(); }
  void emitSeekRequested(qint64 positionMs) { emit seekRequested(positionMs); }
  void requestSeek(qint64 positionMs);
  qlonglong positionUs() const { return state_.positionMs * 1000; }
  qlonglong durationUs() const { return state_.durationMs * 1000; }
  QDBusObjectPath currentTrackId() const;

private:
  void sendPropertiesChanged(const QVariantMap &changed);
  void sendPlaybackStatusChanged();
  void sendMetadataChanged();
  void sendCapabilitiesChanged();
  void sendSeeked(qint64 positionMs);
  QVariantMap buildMetadata() const;
  QString playbackStatusString() const;

  qulonglong trackIdSerial_ = 1;
  std::atomic<uint64_t> artworkTaskId_{0};
  QString artworkUrl_;
  MprisRootAdaptor *rootAdaptor_ = nullptr;
  MprisPlayerAdaptor *playerAdaptor_ = nullptr;
};

#endif // MPRISMEDIAINTERFACE_H
