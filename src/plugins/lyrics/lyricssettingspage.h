#ifndef LYRICSSETTINGSPAGE_H
#define LYRICSSETTINGSPAGE_H

#include "isettingspageprovider.h"
#include <QColor>
#include <QWidget>

class QCheckBox;
class QFontComboBox;
class QPushButton;
class QSpinBox;

// The Lyrics page of the Settings dialog: the lyrics font and highlight
// color, saved as the lyrics/* settings keys.
class LyricsSettingsPage : public QWidget, public IPluginSettingsPage {
  Q_OBJECT

public:
  explicit LyricsSettingsPage(QWidget *parent = nullptr);

  QWidget *widget() override;
  // Saves the settings, then sends applied.
  void apply() override;

signals:
  void applied();

private:
  void updateHighlightColorButton();
  QFontComboBox *fontFamilyCombo_ = nullptr;
  QSpinBox *fontSizeSpin_ = nullptr;
  QCheckBox *useDefaultFontCheck_ = nullptr;
  QPushButton *highlightColorButton_ = nullptr;
  QColor highlightColor_;
};

#endif // LYRICSSETTINGSPAGE_H
