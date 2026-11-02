#ifndef PLAYBACKBUTTONSTYLE_H
#define PLAYBACKBUTTONSTYLE_H

#include <QString>

class QToolButton;

// Turns a playback toolbar button into a fixed-size icon-only button: the SVG
// at iconPath is tinted for the current color scheme, and the button text
// moves to the tooltip and accessible name. Call again after the color scheme
// changes to re-tint the icon.
void setIconOnlyPlaybackButton(QToolButton *button, const QString &iconPath);

#endif // PLAYBACKBUTTONSTYLE_H
