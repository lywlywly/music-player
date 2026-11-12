#include "spectrumwidget.h"

#include <QLinearGradient>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kGridStepDb = 10;
constexpr int kBarGap = 2;

QColor mixed(const QColor &a, const QColor &b, double t) {
  return QColor::fromRgbF(
      static_cast<float>(a.redF() + (b.redF() - a.redF()) * t),
      static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * t),
      static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * t));
}

} // namespace

SpectrumWidget::SpectrumWidget(QWidget *parent) : QWidget(parent) {
  setAutoFillBackground(true);
  setBackgroundRole(QPalette::Base);
}

void SpectrumWidget::setBandLabels(const QStringList &labels, float floorDb) {
  labels_ = labels;
  floorDb_ = floorDb;
  levels_.assign(labels.size(), floorDb);
  peaks_.assign(labels.size(), floorDb);
  update();
}

void SpectrumWidget::setLevels(const std::vector<float> &levels,
                               const std::vector<float> &peaks) {
  levels_ = levels;
  peaks_ = peaks;
  update();
}

void SpectrumWidget::setMessage(const QString &message) {
  if (message != message_) {
    message_ = message;
    update();
  }
}

void SpectrumWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  emit shownChanged(true);
}

void SpectrumWidget::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  emit shownChanged(false);
}

void SpectrumWidget::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  const QFontMetrics metrics = fontMetrics();
  const QColor text = palette().color(QPalette::Text);
  QColor grid = text;
  grid.setAlphaF(0.18f);
  QColor labelColor = text;
  labelColor.setAlphaF(0.65f);

  // The plot, leaving room for the dB labels on the right and the band
  // labels below.
  const int dbLabelWidth =
      metrics.horizontalAdvance(QStringLiteral("-60dB")) + 6;
  const QRect plot = rect().adjusted(0, metrics.height() / 2 + 2, -dbLabelWidth,
                                     -(metrics.height() + 4));
  if (plot.width() <= 0 || plot.height() <= 0) {
    return;
  }
  const auto yForDb = [&](float db) {
    return plot.top() +
           static_cast<int>(std::lround(plot.height() * (db / floorDb_)));
  };

  for (int db = 0; db >= floorDb_; db -= kGridStepDb) {
    const int y = yForDb(static_cast<float>(db));
    painter.setPen(grid);
    painter.drawLine(plot.left(), y, plot.right() + 4, y);
    painter.setPen(labelColor);
    painter.drawText(QRect(plot.right() + 6, y - metrics.height() / 2,
                           dbLabelWidth, metrics.height()),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("%1dB").arg(db));
  }

  const int count = static_cast<int>(labels_.size());
  const double barStep = static_cast<double>(plot.width()) / std::max(1, count);
  // Every labelStep-th band label, so they do not overlap.
  int widestLabel = 0;
  for (const QString &label : labels_) {
    widestLabel = std::max(widestLabel, metrics.horizontalAdvance(label));
  }
  const int labelStep =
      std::max(1, static_cast<int>(std::ceil((widestLabel + 4) / barStep)));

  const QColor top = palette().color(QPalette::Highlight);
  const QColor bottom = mixed(top, text, 0.6);
  QLinearGradient gradient(0, plot.top(), 0, plot.bottom());
  gradient.setColorAt(0.0, top);
  gradient.setColorAt(1.0, bottom);
  QColor peakColor = text;
  peakColor.setAlphaF(0.55f);

  for (int i = 0; i < count; ++i) {
    const int left = plot.left() + static_cast<int>(std::lround(i * barStep));
    const int right = plot.left() +
                      static_cast<int>(std::lround((i + 1) * barStep)) -
                      kBarGap;
    const int width = std::max(1, right - left);
    const int y = yForDb(levels_[i]);
    painter.fillRect(QRect(left, y, width, plot.bottom() - y + 1), gradient);
    if (peaks_[i] > floorDb_) {
      painter.setPen(peakColor);
      const int peakY = yForDb(peaks_[i]);
      painter.drawLine(left, peakY, left + width - 1, peakY);
    }
    if (i % labelStep == 0) {
      // Centered under the bar, but kept inside the plot's width.
      const int labelWidth = metrics.horizontalAdvance(labels_[i]);
      const int labelLeft =
          std::clamp(left + (width - labelWidth) / 2, plot.left(),
                     std::max(plot.left(), plot.right() + 1 - labelWidth));
      painter.setPen(labelColor);
      painter.drawText(
          QRect(labelLeft, plot.bottom() + 3, labelWidth, metrics.height()),
          Qt::AlignLeft | Qt::AlignTop, labels_[i]);
    }
  }

  // Over the bars, on the background color, so it stays readable.
  if (!message_.isEmpty()) {
    const QRect area = plot.adjusted(8, 8, -8, -8);
    const QRect box =
        painter.boundingRect(area, Qt::AlignCenter | Qt::TextWordWrap, message_)
            .adjusted(-6, -4, 6, 4);
    painter.fillRect(box, palette().color(QPalette::Base));
    painter.setPen(text);
    painter.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, message_);
  }
}
