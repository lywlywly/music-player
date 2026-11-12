#ifndef SPECTRUMPRESENTER_H
#define SPECTRUMPRESENTER_H

#include "spectrumanalyzer.h"
#include "spectrumsmoother.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <memory>

class PcmTap;
class SpectrumWidget;

// Drives SpectrumWidget from a PcmTap: about 30 times a second while
// playing, it analyzes the samples at the estimated play position and shows
// the smoothed levels. See docs/spectrum.md.
//
// Active while the widget is shown: it then offers its tap (tapChanged), and
// withdraws it when hidden, so no audio is copied while the tab is hidden.
class SpectrumPresenter : public QObject {
  Q_OBJECT

public:
  // Created before the widget is first shown, so its showEvent activates it.
  explicit SpectrumPresenter(SpectrumWidget *widget, QObject *parent = nullptr);
  ~SpectrumPresenter() override;

  // Playing analyzes; Paused freezes the bars; Stopped lets them fall.
  enum class State { Stopped, Paused, Playing };

  // The play position the player reported.
  void setPosition(qint64 positionMs);
  void setState(State state);

  const std::shared_ptr<PcmTap> &tap() const { return tap_; }
  // Shown when no audio reaches the tap while playing, for kNoAudioDelayMs:
  // the playback backend does not feed it.
  void setNoAudioMessage(const QString &message);
  static constexpr qint64 kNoAudioDelayMs = 1500;

signals:
  // The tap to feed: tap() while active, nullptr otherwise.
  void tapChanged(std::shared_ptr<PcmTap> tap);

private:
  void setActive(bool active);
  void updateTimer();
  void showFrame();
  // Shows or clears the no-audio message, after a frame that had audio or
  // not.
  void updateNoAudioMessage(bool hadAudio);
  // The last reported position plus the time since, while playing: players
  // report the position only every 100 ms or so. If the reports stop, the
  // estimate passes the newest audio in the tap, and the bars fall.
  qint64 estimatedPositionUs() const;

  SpectrumWidget *widget_;
  std::shared_ptr<PcmTap> tap_;
  SpectrumAnalyzer analyzer_;
  SpectrumSmoother smoother_;
  std::vector<float> samples_;
  QTimer frameTimer_;
  QElapsedTimer clock_;
  qint64 positionMs_ = 0;
  // clock_ time of the last position.
  qint64 positionAtMs_ = 0;
  QString noAudioMessage_;
  // clock_ time since which no audio was read while playing, or -1.
  qint64 noAudioSinceMs_ = -1;
  bool active_ = false;
  State state_ = State::Stopped;
};

#endif // SPECTRUMPRESENTER_H
