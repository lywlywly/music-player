#include "playbackbuttonstyle.h"

#include <QColor>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <QStyleHints>
#include <QToolButton>
#include <algorithm>

namespace {
constexpr int kPlaybackIconSize = 18;

QColor playbackIconColor() {
  if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark) {
    return QColor(QStringLiteral("#fcfcfc"));
  }
  return QColor(QStringLiteral("#232629"));
}

QIcon tintedIcon(const QString &iconPath, const QColor &color,
                 qreal iconRenderScale) {
  const QSize iconSize(kPlaybackIconSize, kPlaybackIconSize);
  const qreal renderScale = std::max<qreal>(1.0, iconRenderScale);
  const QRect logicalRect(QPoint(0, 0), iconSize);
  QPixmap pixmap(iconSize * renderScale);
  pixmap.setDevicePixelRatio(renderScale);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.drawPixmap(logicalRect, QIcon(iconPath).pixmap(pixmap.size()));
  painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
  painter.fillRect(logicalRect, color);
  return QIcon(pixmap);
}

QString playbackButtonStyleSheet() {
  const bool dark =
      QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
  const QString hover = dark ? QStringLiteral("rgba(255, 255, 255, 0.12)")
                             : QStringLiteral("rgba(0, 0, 0, 0.08)");
  const QString pressed = dark ? QStringLiteral("rgba(255, 255, 255, 0.22)")
                               : QStringLiteral("rgba(0, 0, 0, 0.16)");
  return QStringLiteral("QToolButton { border: none; border-radius: 4px;"
                        " background: transparent; padding: 0px; }"
                        "QToolButton:hover:enabled { background: %1; }"
                        "QToolButton:pressed:enabled { background: %2; }")
      .arg(hover, pressed);
}
} // namespace

void setIconOnlyPlaybackButton(QToolButton *button, const QString &iconPath) {
  const QString label =
      button->text().isEmpty() ? button->toolTip() : button->text();
  const qreal iconRenderScale = button->devicePixelRatioF();
  button->setIcon(tintedIcon(iconPath, playbackIconColor(), iconRenderScale));
  button->setText(QString());
  button->setToolTip(label);
  button->setAccessibleName(label);
  button->setIconSize(QSize(kPlaybackIconSize, kPlaybackIconSize));
  button->setFixedSize(24, 24);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setStyleSheet(playbackButtonStyleSheet());
}
