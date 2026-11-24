#ifndef COVERARTPRESENTER_H
#define COVERARTPRESENTER_H

#include "songlibrary.h"
#include <QByteArray>
#include <QObject>
#include <QPixmap>

class QLabel;

// Shows the current song's embedded cover image in the main window's cover
// label, scaled to the label whenever it is resized, wherever the layout puts
// it. Frontend class: MainWindow tells it when a song starts or stops.
class CoverArtPresenter : public QObject {
  Q_OBJECT

public:
  explicit CoverArtPresenter(QLabel *coverLabel, QObject *parent = nullptr);

  // Shows the song's embedded cover, or "No cover" when it has none.
  void showSong(const MSong &song);
  void clear();

signals:
  // The new song's embedded cover image. Not emitted when it has none.
  void artworkChanged(const QByteArray &imageData);

protected:
  // Rescales the cover on the label's resize.
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  // Scales the cover to the label's current size.
  void updateCoverSize();

  QLabel *coverLabel_;
  QPixmap cover_;
};

#endif // COVERARTPRESENTER_H
