#include "macosappearance.h"

#include <AppKit/AppKit.h>

namespace macos {
void applyAppAppearance(AppearanceMode mode) {
  if (@available(macOS 10.14, *)) {
    switch (mode) {
    case AppearanceMode::Light:
      NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
      break;
    case AppearanceMode::Dark:
      NSApp.appearance =
          [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
      break;
    case AppearanceMode::System:
      NSApp.appearance = nil;
      break;
    }
  }
}
} // namespace macos
