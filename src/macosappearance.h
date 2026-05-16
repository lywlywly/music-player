#ifndef MACOSAPPEARANCE_H
#define MACOSAPPEARANCE_H

namespace macos {
enum class AppearanceMode { System, Light, Dark };

void applyAppAppearance(AppearanceMode mode);
} // namespace macos

#endif // MACOSAPPEARANCE_H
