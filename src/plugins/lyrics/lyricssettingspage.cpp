#include "lyricssettingspage.h"

#include "lyricssettings.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

LyricsSettingsPage::LyricsSettingsPage(QWidget *parent) : QWidget(parent) {
  const LyricsSettings initial = loadLyricsSettings();
  highlightColor_ = initial.highlightColor;

  QVBoxLayout *layout = new QVBoxLayout(this);
  QLabel *note = new QLabel("Change lyrics panel font family and size.", this);
  note->setWordWrap(true);
  layout->addWidget(note);
  QFormLayout *form = new QFormLayout();
  fontFamilyCombo_ = new QFontComboBox(this);
  fontSizeSpin_ = new QSpinBox(this);
  useDefaultFontCheck_ = new QCheckBox("Use system default font", this);
  highlightColorButton_ = new QPushButton(this);
  fontSizeSpin_->setRange(8, 48);
  fontSizeSpin_->setValue(initial.fontSize);

  // The combo selects the font Qt uses for the family: the family itself if
  // installed, else its substitute.
  if (!initial.fontFamily.isEmpty()) {
    fontFamilyCombo_->setCurrentFont(QFont(initial.fontFamily));
  }
  useDefaultFontCheck_->setChecked(initial.useSystemDefaultFont);
  fontFamilyCombo_->setEnabled(!initial.useSystemDefaultFont);
  connect(useDefaultFontCheck_, &QCheckBox::toggled, fontFamilyCombo_,
          &QWidget::setDisabled);
  updateHighlightColorButton();
  connect(highlightColorButton_, &QPushButton::clicked, this, [this]() {
    const QColor selected = QColorDialog::getColor(
        highlightColor_, this, "Select lyrics highlight color");
    if (!selected.isValid()) {
      return;
    }
    highlightColor_ = selected;
    updateHighlightColorButton();
  });
  form->addRow("Font:", fontFamilyCombo_);
  form->addRow("", useDefaultFontCheck_);
  form->addRow("Size:", fontSizeSpin_);
  form->addRow("Highlight color:", highlightColorButton_);
  layout->addLayout(form);
  layout->addStretch();
}

QWidget *LyricsSettingsPage::widget() { return this; }

void LyricsSettingsPage::apply() {
  // A typed family that is not installed, or none, is kept as typed: the
  // panel's font then falls back to the application font's families.
  saveLyricsSettings({.fontFamily = fontFamilyCombo_->currentText(),
                      .useSystemDefaultFont = useDefaultFontCheck_->isChecked(),
                      .fontSize = fontSizeSpin_->value(),
                      .highlightColor = highlightColor_});
  emit applied();
}

void LyricsSettingsPage::updateHighlightColorButton() {
  const QString colorHex = highlightColor_.name(QColor::HexRgb);
  highlightColorButton_->setText(colorHex);

  QPixmap swatch(20, 12);
  swatch.fill(Qt::transparent);
  QPainter painter(&swatch);
  painter.setPen(Qt::black);
  painter.setBrush(highlightColor_);
  painter.drawRect(0, 0, swatch.width() - 1, swatch.height() - 1);
  painter.end();

  highlightColorButton_->setIcon(QIcon(swatch));
  highlightColorButton_->setIconSize(swatch.size());
}
