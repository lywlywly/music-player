#ifndef SYSTEMMEDIAPRESENTER_H
#define SYSTEMMEDIAPRESENTER_H

#include "songlibrary.h"
#include <QByteArray>
#include <QObject>

class ISystemMediaInterface;
class PlaybackController;

// Keeps the OS media controls (ISystemMediaInterface) in sync with
// PlaybackController's state, duration and position, and sends their requests
// to it. Frontend class: MainWindow tells it when a song starts and gives it
// the song's artwork.
class SystemMediaPresenter : public QObject {
  Q_OBJECT

public:
  SystemMediaPresenter(ISystemMediaInterface &media,
                       PlaybackController &playback, QObject *parent = nullptr);

  // Publishes the song's title and artist, which clears the artwork, so call
  // it before setArtwork for the same song.
  void showSong(const MSong &song);
  void setArtwork(const QByteArray &imageData);

private:
  ISystemMediaInterface &media_;
};

#endif // SYSTEMMEDIAPRESENTER_H
