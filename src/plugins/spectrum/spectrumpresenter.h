#ifndef SPECTRUMPRESENTER_H
#define SPECTRUMPRESENTER_H

#include "iplayerlistener.h"
#include "spectrumanalyzer.h"
#include "spectrumsmoother.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class IAudioHost;
class IPlayerHost;
class SpectrumWidget;

// Drives SpectrumWidget from the host's audio: about 30 times a second while
// playing, it analyzes the samples being heard and shows the smoothed levels.
// See docs/plugins/spectrum.md.
//
// Active while the widget is shown: it then wants the host's audio, and stops
// wanting it when hidden, so no audio is copied while the panel is hidden. It
// listens to the player host for its lifetime; its owner destroys it before
// the plugin shuts down.
class SpectrumPresenter : public QObject, public IPlayerListener {
  Q_OBJECT

public:
  // Created before the widget is first shown, so its showEvent activates it.
  SpectrumPresenter(SpectrumWidget *widget, IPlayerHost &player,
                    IAudioHost &audio, QObject *parent = nullptr);
  ~SpectrumPresenter() override;

  // Playing analyzes; a pause freezes the bars; a stop lets them fall.
  void trackStarted(const TrackInfo &track) override;
  void resumed() override;
  void paused() override;
  void stopped() override;

  // The number of bands, one of SpectrumWidget::kBandCounts, saved in the
  // spectrum/bands setting. The widget's context menu sets it.
  void setBandCount(int count);
  int bandCount() const { return analyzer_.bands().count(); }

  // Shown when no audio is read while playing, for kNoAudioDelayMs: the
  // playback backend does not feed it.
  static constexpr qint64 kNoAudioDelayMs = 1500;

private:
  enum class State { Stopped, Paused, Playing };

  void setState(State state);
  void setActive(bool active);
  // Sends the band labels to the widget, which puts its bars at the floor.
  void showBandLabels();
  void updateTimer();
  void showFrame();
  // Shows or clears the no-audio message, after a frame that had audio or
  // not.
  void updateNoAudioMessage(bool hadAudio);

  SpectrumWidget *widget_;
  IPlayerHost &player_;
  IAudioHost &audio_;
  SpectrumAnalyzer analyzer_;
  SpectrumSmoother smoother_;
  std::vector<float> samples_;
  QTimer frameTimer_;
  QElapsedTimer clock_;
  // clock_ time since which no audio was read while playing, or -1.
  qint64 noAudioSinceMs_ = -1;
  bool active_ = false;
  State state_ = State::Stopped;
};

#endif // SPECTRUMPRESENTER_H
