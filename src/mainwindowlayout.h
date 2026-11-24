#ifndef MAINWINDOWLAYOUT_H
#define MAINWINDOWLAYOUT_H

#include "layout/panellayout.h"

// The app's side of the main window's layout (docs/layout-editing.md): the
// ids of the elements MainWindow registers, and where they start.
namespace MainWindowLayout {
// The built-in elements' ids. A plugin panel's id is its plugin's id.
inline constexpr const char *kPlaylist = "playlist";
inline constexpr const char *kProperties = "properties";
inline constexpr const char *kSpectrum = "spectrum";
inline constexpr const char *kCover = "cover";

// Properties above the cover on the left, at a quarter of the width, and the
// playlist on the right. The other elements start outside the tree.
PanelLayout defaultLayout();
} // namespace MainWindowLayout

#endif // MAINWINDOWLAYOUT_H
