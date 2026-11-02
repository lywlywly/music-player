#ifndef DISPLAYTHEME_H
#define DISPLAYTHEME_H

#include <QString>

// The display theme mode ("system", "light" or "dark"), used by MainWindow and
// SettingsDialog.

// The stored mode (trimmed, lowercase), or "system" when none is stored.
QString loadDisplayThemeModeSetting();
void saveDisplayThemeModeSetting(const QString &mode);

// Sets the application color scheme: "light", "dark", or anything else to
// follow the system.
void applyDisplayThemeMode(const QString &mode);

#endif // DISPLAYTHEME_H
