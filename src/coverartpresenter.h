#ifndef COVERARTPRESENTER_H
#define COVERARTPRESENTER_H

#include "songlibrary.h"
#include <QByteArray>
#include <QObject>
#include <QPixmap>

class QLabel;

// Shows the current song's embedded cover image in the main window's cover
// label, scaled to the label. Frontend class: MainWindow tells it when a song
// starts or stops.
class CoverArtPresenter : public QObject {
  Q_OBJECT

public:
  explicit CoverArtPresenter(QLabel *coverLabel, QObject *parent = nullptr);

  // Shows the song's embedded cover, or "No cover" when it has none.
  void showSong(const MSong &song);
  void clear();
  // Scales the cover to the label's current size, e.g. after a resize or a
  // splitter move.
  void updateCoverSize();

signals:
  // The new song's embedded cover image. Not emitted when it has none.
  void artworkChanged(const QByteArray &imageData);

private:
  QLabel *coverLabel_;
  QPixmap cover_;
};

#endif // COVERARTPRESENTER_H
