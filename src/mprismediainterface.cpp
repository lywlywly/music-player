#include "mprismediainterface.h"
#include "artworkutils.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QStandardPaths>
#include <QUrl>
#include <thread>

MprisRootAdaptor::MprisRootAdaptor(MprisMediaInterface *parent)
    : QDBusAbstractAdaptor(parent) {}

MprisPlayerAdaptor::MprisPlayerAdaptor(MprisMediaInterface *parent)
    : QDBusAbstractAdaptor(parent), backend_(parent) {}

void MprisPlayerAdaptor::Play() { backend_->emitPlayRequested(); }
void MprisPlayerAdaptor::Pause() { backend_->emitPauseRequested(); }
void MprisPlayerAdaptor::PlayPause() { backend_->emitToggleRequested(); }
void MprisPlayerAdaptor::Next() { backend_->emitNextRequested(); }
void MprisPlayerAdaptor::Previous() { backend_->emitPreviousRequested(); }
qlonglong MprisPlayerAdaptor::position() const {
  return backend_->positionUs();
}
void MprisPlayerAdaptor::Seek(qlonglong offsetUs) {
  const qlonglong currentUs = backend_->positionUs();
  const qlonglong targetUs = currentUs + offsetUs;
  backend_->requestSeek(targetUs / 1000);
}
void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &,
                                     qlonglong positionUs) {
  backend_->requestSeek(positionUs / 1000);
}

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

  sendCapabilitiesChanged();
  sendPlaybackStatusChanged();
  sendMetadataChanged();
}

void MprisMediaInterface::setTitleAndArtist(const QString &title,
                                            const QString &artist) {
  ++trackIdSerial_;
  ++artworkTaskId_;

  state_.title = title;
  state_.artist = artist;
  state_.playbackState = PlaybackState::Playing;
  state_.positionMs = 0;
  state_.artworkData.clear();
  artworkUrl_.clear();

  sendMetadataChanged();
  sendPlaybackStatusChanged();
  sendSeeked(0);
}

void MprisMediaInterface::setDuration(qint64 durationMs) {
  state_.durationMs = durationMs;
  sendMetadataChanged();
}

void MprisMediaInterface::setPlaybackState(PlaybackState state) {
  if (state_.playbackState == state) {
    return;
  }

  state_.playbackState = state;
  if (state == PlaybackState::Stopped) {
    state_.positionMs = 0;
    sendPlaybackStatusChanged();
    sendSeeked(0);
    return;
  }

  sendPlaybackStatusChanged();
}

QVariantMap MprisMediaInterface::buildMetadata() const {
  QVariantMap m;
  m[QStringLiteral("mpris:trackid")] = QVariant::fromValue(currentTrackId());
  m[QStringLiteral("xesam:title")] = state_.title;
  m[QStringLiteral("xesam:artist")] = QStringList{state_.artist};
  m[QStringLiteral("mpris:length")] = state_.durationMs * 1000;
  if (!artworkUrl_.isEmpty()) {
    m[QStringLiteral("mpris:artUrl")] = artworkUrl_;
  }
  return m;
}

QDBusObjectPath MprisMediaInterface::currentTrackId() const {
  // MPRIS requires a stable object-path identifier for the current track, so
  // we expose a synthetic path that changes whenever the track changes.
  return QDBusObjectPath(
      QStringLiteral("/org/mpris/MediaPlayer2/Track/%1").arg(trackIdSerial_));
}

void MprisMediaInterface::requestSeek(qint64 positionMs) {
  emitSeekRequested(positionMs);
}

void MprisMediaInterface::setPosition(qint64 positionMs) {
  state_.positionMs = positionMs;
  sendSeeked(positionMs);
}

void MprisMediaInterface::setArtwork(const QByteArray &imageData) {
  const uint64_t taskId = ++artworkTaskId_;

  if (imageData.isEmpty()) {
    if (state_.artworkData.isEmpty() && artworkUrl_.isEmpty()) {
      return;
    }
    state_.artworkData.clear();
    artworkUrl_.clear();
    sendMetadataChanged();
    return;
  }

  QPointer<MprisMediaInterface> self(this);
  const QByteArray imageDataCopy = imageData;
  std::thread([self, taskId, imageDataCopy]() {
    const QByteArray preparedArtworkPng =
        ArtworkUtils::normalizeArtworkToPng(imageDataCopy, 512);
    QString preparedArtworkUrl;

    if (!preparedArtworkPng.isEmpty()) {
      QString cacheDir =
          QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
      if (cacheDir.isEmpty()) {
        cacheDir =
            QStandardPaths::writableLocation(QStandardPaths::TempLocation);
      }
      QDir dir(cacheDir);
      if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
      }

      const QString artworkPath =
          dir.filePath(QStringLiteral("myplayer_mpris_artwork.png"));
      QFile artworkFile(artworkPath);
      if (artworkFile.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
          artworkFile.write(preparedArtworkPng) == preparedArtworkPng.size()) {
        artworkFile.close();
        preparedArtworkUrl = QUrl::fromLocalFile(artworkPath).toString();
      }
    }

    if (!self) {
      return;
    }
    QMetaObject::invokeMethod(
        self,
        [self, taskId, preparedArtworkPng, preparedArtworkUrl]() {
          if (!self || taskId != self->artworkTaskId_.load()) {
            return;
          }
          if (preparedArtworkPng.isEmpty() || preparedArtworkUrl.isEmpty()) {
            self->state_.artworkData.clear();
            self->artworkUrl_.clear();
            self->sendMetadataChanged();
            return;
          }
          if (self->state_.artworkData == preparedArtworkPng &&
              self->artworkUrl_ == preparedArtworkUrl) {
            return;
          }
          self->state_.artworkData = preparedArtworkPng;
          self->artworkUrl_ = preparedArtworkUrl;
          self->sendMetadataChanged();
        },
        Qt::QueuedConnection);
  }).detach();
}

void MprisMediaInterface::sendPropertiesChanged(const QVariantMap &changed) {
  if (changed.isEmpty()) {
    return;
  }

  QDBusMessage msg = QDBusMessage::createSignal(
      QStringLiteral("/org/mpris/MediaPlayer2"),
      QStringLiteral("org.freedesktop.DBus.Properties"),
      QStringLiteral("PropertiesChanged"));

  msg << QStringLiteral("org.mpris.MediaPlayer2.Player");
  msg << changed;
  msg << QStringList{};

  QDBusConnection::sessionBus().send(msg);
}

void MprisMediaInterface::sendPlaybackStatusChanged() {
  QVariantMap changed;
  changed[QStringLiteral("PlaybackStatus")] = playbackStatusString();
  sendPropertiesChanged(changed);
}

void MprisMediaInterface::sendMetadataChanged() {
  QVariantMap changed;
  changed[QStringLiteral("Metadata")] = buildMetadata();
  sendPropertiesChanged(changed);
}

void MprisMediaInterface::sendCapabilitiesChanged() {
  QVariantMap changed;
  changed[QStringLiteral("CanControl")] = true;
  changed[QStringLiteral("CanPlay")] = true;
  changed[QStringLiteral("CanPause")] = true;
  changed[QStringLiteral("CanGoNext")] = true;
  changed[QStringLiteral("CanGoPrevious")] = true;
  changed[QStringLiteral("CanSeek")] = true;
  sendPropertiesChanged(changed);
}

void MprisMediaInterface::sendSeeked(qint64 positionMs) {
  QDBusMessage msg = QDBusMessage::createSignal(
      QStringLiteral("/org/mpris/MediaPlayer2"),
      QStringLiteral("org.mpris.MediaPlayer2.Player"),
      QStringLiteral("Seeked"));
  msg << positionMs * 1000;
  QDBusConnection::sessionBus().send(msg);
}

QString MprisMediaInterface::playbackStatusString() const {
  switch (state_.playbackState) {
  case PlaybackState::Playing:
    return QStringLiteral("Playing");
  case PlaybackState::Paused:
    return QStringLiteral("Paused");
  case PlaybackState::Stopped:
    return QStringLiteral("Stopped");
  }

  return QStringLiteral("Stopped");
}
