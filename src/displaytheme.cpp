#include "displaytheme.h"

#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

namespace {
constexpr const char *kDisplayThemeModeSettingsKey = "display/theme_mode";
} // namespace

QString loadDisplayThemeModeSetting() {
  QSettings settings;
  return settings.value(kDisplayThemeModeSettingsKey, QStringLiteral("system"))
      .toString()
      .trimmed()
      .toLower();
}

void saveDisplayThemeModeSetting(const QString &mode) {
  QSettings settings;
  settings.setValue(kDisplayThemeModeSettingsKey, mode);
}

void applyDisplayThemeMode(const QString &mode) {
  Qt::ColorScheme scheme = Qt::ColorScheme::Unknown;
  if (mode == QStringLiteral("dark")) {
    scheme = Qt::ColorScheme::Dark;
  } else if (mode == QStringLiteral("light")) {
    scheme = Qt::ColorScheme::Light;
  }
  QGuiApplication::styleHints()->setColorScheme(scheme);
}
