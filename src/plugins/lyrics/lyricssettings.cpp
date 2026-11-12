#include "lyricssettings.h"

#include <QApplication>
#include <QFont>
#include <QSettings>

namespace {
const QColor kDefaultHighlightColor(0, 100, 255);
} // namespace

LyricsSettings loadLyricsSettings() {
  QSettings settings;
  LyricsSettings result;
  result.fontFamily =
      settings.value("lyrics/font_family", QString()).toString();
  result.useSystemDefaultFont =
      settings
          .value("lyrics/use_system_default_font", result.fontFamily.isEmpty())
          .toBool();
  result.fontSize =
      settings.value("lyrics/font_size", QApplication::font().pointSize())
          .toInt();
  result.highlightColor =
      QColor(settings
                 .value("lyrics/highlight_color",
                        kDefaultHighlightColor.name(QColor::HexRgb))
                 .toString());
  if (!result.highlightColor.isValid()) {
    result.highlightColor = kDefaultHighlightColor;
  }
  return result;
}

void saveLyricsSettings(const LyricsSettings &settings) {
  QSettings store;
  store.setValue("lyrics/use_system_default_font",
                 settings.useSystemDefaultFont);
  store.setValue("lyrics/font_family", settings.fontFamily);
  store.setValue("lyrics/font_size", settings.fontSize);
  store.setValue("lyrics/highlight_color",
                 settings.highlightColor.name(QColor::HexRgb));
}
