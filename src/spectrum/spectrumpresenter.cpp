#include "spectrumpresenter.h"

#include "pcmtap.h"
#include "spectrumwidget.h"

namespace {
constexpr int kFrameIntervalMs = 33;
} // namespace

SpectrumPresenter::SpectrumPresenter(SpectrumWidget *widget, QObject *parent)
    : QObject(parent), widget_(widget), tap_(std::make_shared<PcmTap>()),
      analyzer_(SpectrumBands::logSpaced(50.0, 20000.0, 20)),
      smoother_(analyzer_.bands().count(), SpectrumAnalyzer::kFloorDb),
      samples_(analyzer_.fftSize()),
      noAudioMessage_(tr("No audio from the playback backend.")) {
  clock_.start();
  frameTimer_.setInterval(kFrameIntervalMs);
  connect(&frameTimer_, &QTimer::timeout, this, &SpectrumPresenter::showFrame);

  QStringList labels;
  for (double center : analyzer_.bands().centers) {
    labels << QString::fromStdString(frequencyLabel(center));
  }
  widget_->setBandLabels(labels, SpectrumAnalyzer::kFloorDb);
  connect(widget_, &SpectrumWidget::shownChanged, this,
          &SpectrumPresenter::setActive);
}

SpectrumPresenter::~SpectrumPresenter() = default;

void SpectrumPresenter::setNoAudioMessage(const QString &message) {
  noAudioMessage_ = message;
}

void SpectrumPresenter::setPosition(qint64 positionMs) {
  positionMs_ = positionMs;
  positionAtMs_ = clock_.elapsed();
}

void SpectrumPresenter::setState(State state) {
  state_ = state;
  if (state_ == State::Stopped) {
    updateNoAudioMessage(true);
  }
  // The position is reported again soon; restart the estimate from now.
  positionAtMs_ = clock_.elapsed();
  updateTimer();
}

void SpectrumPresenter::setActive(bool active) {
  active_ = active;
  emit tapChanged(active_ ? tap_ : nullptr);
  if (!active_) {
    // Hidden: forget the audio (after withdrawing the tap, so the player does
    // not refill it), so the tab starts clean when shown again.
    tap_->clear();
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

qint64 SpectrumPresenter::estimatedPositionUs() const {
  qint64 ms = positionMs_;
  if (state_ == State::Playing) {
    ms += clock_.elapsed() - positionAtMs_;
  }
  return ms * 1000;
}

void SpectrumPresenter::showFrame() {
  const double nowSeconds = clock_.elapsed() / 1000.0;
  std::vector<float> targets(analyzer_.bands().count(),
                             SpectrumAnalyzer::kFloorDb);
  if (state_ == State::Playing) {
    const int sampleRate = tap_->read(estimatedPositionUs(), samples_);
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
  widget_->setMessage(show ? noAudioMessage_ : QString());
}
