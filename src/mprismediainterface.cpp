#include "mprismediainterface.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDebug>

MprisRootAdaptor::MprisRootAdaptor(MprisMediaInterface *parent)
    : QDBusAbstractAdaptor(parent) {}

MprisPlayerAdaptor::MprisPlayerAdaptor(MprisMediaInterface *parent)
    : QDBusAbstractAdaptor(parent), backend_(parent) {}

void MprisPlayerAdaptor::Play() { backend_->emitPlayRequested(); }
void MprisPlayerAdaptor::Pause() { backend_->emitPauseRequested(); }
void MprisPlayerAdaptor::PlayPause() { backend_->emitToggleRequested(); }
void MprisPlayerAdaptor::Next() { backend_->emitNextRequested(); }
void MprisPlayerAdaptor::Previous() { backend_->emitPreviousRequested(); }

MprisMediaInterface::MprisMediaInterface(QObject *parent)
    : ISystemMediaInterface(parent) {

  const QString service = QStringLiteral("org.mpris.MediaPlayer2.myplayer");
  const QString path = QStringLiteral("/org/mpris/MediaPlayer2");

  QDBusConnection bus = QDBusConnection::sessionBus();

  if (!bus.registerService(service)) {
    qWarning() << "Cannot register MPRIS service" << service << ":"
               << bus.lastError().message();
  }

  // Export this QObject with all adaptors
  if (!bus.registerObject(path, this, QDBusConnection::ExportAdaptors)) {
    qWarning() << "Cannot register MPRIS object at" << path << ":"
               << bus.lastError().message();
  }

  // Create adaptors (they attach to 'this')
  rootAdaptor_ = new MprisRootAdaptor(this);
  playerAdaptor_ = new MprisPlayerAdaptor(this);
}

void MprisMediaInterface::updateNowPlaying(const QString &title,
                                           const QString &artist,
                                           qint64 durationMs, qint64 positionMs,
                                           bool isPlaying) {
  state_.title = title;
  state_.artist = artist;
  state_.durationMs = durationMs;
  state_.positionMs = positionMs;
  state_.isPlaying = isPlaying;

  sendPropertiesChanged();
}

void MprisMediaInterface::setTrackInfo(const QString &title,
                                       const QString &artist,
                                       qint64 durationMs) {
  state_.title = title;
  state_.artist = artist;
  state_.durationMs = durationMs;
  state_.isPlaying = true;

  sendPropertiesChanged();
}

void MprisMediaInterface::updatePosition(qint64 positionMs) {
  state_.positionMs = positionMs;
  sendPropertiesChanged();
}

void MprisMediaInterface::updatePlaybackState(bool isPlaying) {
  state_.isPlaying = isPlaying;
  sendPropertiesChanged();
}

QVariantMap MprisMediaInterface::buildMetadata() const {
  QVariantMap m;
  m[QStringLiteral("xesam:title")] = state_.title;
  m[QStringLiteral("xesam:artist")] = QStringList{state_.artist};
  m[QStringLiteral("mpris:length")] = state_.durationMs * 1000;
  return m;
}

void MprisMediaInterface::sendPropertiesChanged() {
  QVariantMap changed;

  changed[QStringLiteral("PlaybackStatus")] =
      state_.isPlaying ? QStringLiteral("Playing") : QStringLiteral("Paused");

  changed[QStringLiteral("Metadata")] = buildMetadata();
  changed[QStringLiteral("Position")] = state_.positionMs * 1000;

  changed[QStringLiteral("CanControl")] = true;
  changed[QStringLiteral("CanPlay")] = true;
  changed[QStringLiteral("CanPause")] = true;
  changed[QStringLiteral("CanGoNext")] = true;
  changed[QStringLiteral("CanGoPrevious")] = true;

  QDBusMessage msg = QDBusMessage::createSignal(
      QStringLiteral("/org/mpris/MediaPlayer2"),
      QStringLiteral("org.freedesktop.DBus.Properties"),
      QStringLiteral("PropertiesChanged"));

  msg << QStringLiteral("org.mpris.MediaPlayer2.Player");
  msg << changed;
  msg << QStringList{};

  QDBusConnection::sessionBus().send(msg);
}
