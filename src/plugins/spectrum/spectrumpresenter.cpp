#include "spectrumpresenter.h"

#include "iaudiohost.h"
#include "iplayerhost.h"
#include "spectrumwidget.h"
#include <QSettings>
#include <algorithm>

namespace {
constexpr int kFrameIntervalMs = 33;
constexpr int kDefaultBandCount = 20;
constexpr auto kBandCountKey = "spectrum/bands";

// The saved band count, or the default when none (or an unknown one) is saved.
int savedBandCount() {
  const int saved = QSettings().value(kBandCountKey, kDefaultBandCount).toInt();
  return std::ranges::find(SpectrumWidget::kBandCounts, saved) !=
                 SpectrumWidget::kBandCounts.end()
             ? saved
             : kDefaultBandCount;
}
} // namespace

SpectrumPresenter::SpectrumPresenter(SpectrumWidget *widget,
                                     IPlayerHost &player, IAudioHost &audio,
                                     QObject *parent)
    : QObject(parent), widget_(widget), player_(player), audio_(audio),
      analyzer_(SpectrumBands::fractionalOctave(savedBandCount())),
      smoother_(analyzer_.bands().count(), SpectrumAnalyzer::kFloorDb),
      samples_(analyzer_.fftSize()) {
  clock_.start();
  frameTimer_.setInterval(kFrameIntervalMs);
  connect(&frameTimer_, &QTimer::timeout, this, &SpectrumPresenter::showFrame);

  showBandLabels();
  connect(widget_, &SpectrumWidget::shownChanged, this,
          &SpectrumPresenter::setActive);
  connect(widget_, &SpectrumWidget::bandCountRequested, this,
          &SpectrumPresenter::setBandCount);
  player_.addListener(this);
}

SpectrumPresenter::~SpectrumPresenter() {
  player_.removeListener(this);
  // Destroyed with a panel that is still shown (no hideEvent): the host
  // stops copying audio for it.
  if (active_) {
    audio_.setWanted(false);
  }
}

void SpectrumPresenter::trackStarted(const TrackInfo & /*track*/) {
  setState(State::Playing);
}

void SpectrumPresenter::resumed() { setState(State::Playing); }

void SpectrumPresenter::paused() { setState(State::Paused); }

void SpectrumPresenter::stopped() { setState(State::Stopped); }

void SpectrumPresenter::setBandCount(int count) {
  if (count == bandCount()) {
    return;
  }
  QSettings().setValue(kBandCountKey, count);
  // The bars restart from the floor; while playing, the next frame raises
  // them to the new bands' levels.
  analyzer_ = SpectrumAnalyzer(SpectrumBands::fractionalOctave(count));
  smoother_ = SpectrumSmoother(count, SpectrumAnalyzer::kFloorDb);
  showBandLabels();
  updateTimer();
}

void SpectrumPresenter::showBandLabels() {
  QStringList labels;
  for (double center : analyzer_.bands().centers) {
    labels << QString::fromStdString(frequencyLabel(center));
  }
  widget_->setBandLabels(labels, SpectrumAnalyzer::kFloorDb);
}

void SpectrumPresenter::setState(State state) {
  state_ = state;
  if (state_ == State::Stopped) {
    updateNoAudioMessage(true);
  }
  updateTimer();
}

void SpectrumPresenter::setActive(bool active) {
  active_ = active;
  // Hidden: the host forgets the audio once no plugin wants it, so the panel
  // starts clean when shown again.
  audio_.setWanted(active_);
  if (!active_) {
    smoother_.reset();
    updateNoAudioMessage(true);
    widget_->setLevels(smoother_.levels(), smoother_.peaks());
  }
  updateTimer();
}

void SpectrumPresenter::updateTimer() {
  const bool run =
      active_ && (state_ == State::Playing ||
                  (state_ == State::Stopped && !smoother_.isAtFloor()));
  if (run && !frameTimer_.isActive()) {
    frameTimer_.start();
  } else if (!run) {
    frameTimer_.stop();
  }
}

void SpectrumPresenter::showFrame() {
  const double nowSeconds = clock_.elapsed() / 1000.0;
  std::vector<float> targets(analyzer_.bands().count(),
                             SpectrumAnalyzer::kFloorDb);
  if (state_ == State::Playing) {
    const int sampleRate = audio_.read(samples_);
    if (sampleRate > 0) {
      targets = analyzer_.analyze(samples_, sampleRate);
    }
    updateNoAudioMessage(sampleRate > 0);
  }
  smoother_.update(targets, nowSeconds);
  widget_->setLevels(smoother_.levels(), smoother_.peaks());
  updateTimer();
}

void SpectrumPresenter::updateNoAudioMessage(bool hadAudio) {
  // Short gaps (just after a seek, past the end of a song) are normal.
  bool show = false;
  if (hadAudio) {
    noAudioSinceMs_ = -1;
  } else if (noAudioSinceMs_ < 0) {
    noAudioSinceMs_ = clock_.elapsed();
  } else {
    show = clock_.elapsed() - noAudioSinceMs_ >= kNoAudioDelayMs;
  }
  // Only the GStreamer backend feeds the audio everywhere; the Qt one does
  // only where Qt has its FFmpeg media backend (docs/plugins/spectrum.md).
  widget_->setMessage(
      show ? tr("No audio for the spectrum from this playback backend. Choose "
                "GStreamer in Settings > Playback > Playback backend.")
           : QString());
}
