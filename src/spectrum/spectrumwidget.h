#ifndef SPECTRUMWIDGET_H
#define SPECTRUMWIDGET_H

#include <QStringList>
#include <QWidget>
#include <vector>

// The Spectrum tab: one bar per band, with a peak mark above it, on a dB
// scale from 0 at the top to floorDb at the bottom, with a grid line and a
// label every 10 dB on the right and the band labels below. Paints what it is
// given; SpectrumPresenter decides what. Colors come from the palette.
class SpectrumWidget : public QWidget {
  Q_OBJECT

public:
  explicit SpectrumWidget(QWidget *parent = nullptr);

  void setBandLabels(const QStringList &labels, float floorDb);
  // One level and one peak per band label, in [floorDb, 0] dB.
  void setLevels(const std::vector<float> &levels,
                 const std::vector<float> &peaks);
  const std::vector<float> &levels() const { return levels_; }
  const std::vector<float> &peaks() const { return peaks_; }
  // Shown over the bars when not empty.
  void setMessage(const QString &message);
  const QString &message() const { return message_; }

signals:
  // From showEvent / hideEvent. In a QTabWidget only the current page is
  // shown, so this says whether the tab is in front.
  void shownChanged(bool shown);

protected:
  void paintEvent(QPaintEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  QStringList labels_;
  float floorDb_ = -60.0f;
  std::vector<float> levels_;
  std::vector<float> peaks_;
  QString message_;
};

#endif // SPECTRUMWIDGET_H
