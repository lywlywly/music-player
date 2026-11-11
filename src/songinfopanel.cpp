#include "songinfopanel.h"

#include <QBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>

namespace {

// Ignored: the buttons must not set the panel's minimum width (its column can
// be as narrow as 120 px); resizeEvent puts them side by side only when both
// fit.
QPushButton *makeButton(const QString &text, QWidget *parent) {
  auto *button = new QPushButton(text, parent);
  button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  button->setEnabled(false);
  return button;
}

// Wraps at spaces, and inside paths at their separators, so a long path is
// shown in full.
QLabel *makeValueLabel(const SongInfoRow &row, QWidget *parent) {
  auto *label = new QLabel(row.value, parent);
  label->setTextFormat(Qt::PlainText);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setWordWrap(true);
  label->setToolTip(row.toolTip);
  return label;
}

} // namespace

SongInfoPanel::SongInfoPanel(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  placeholder_ = new QLabel(tr("No song selected"), this);
  placeholder_->setAlignment(Qt::AlignCenter);
  placeholder_->setEnabled(false);
  layout->addWidget(placeholder_, 1);

  scrollArea_ = new QScrollArea(this);
  scrollArea_->setWidgetResizable(true);
  scrollArea_->setFrameShape(QFrame::NoFrame);
  // Values wrap, but a word or path segment longer than the column can't:
  // scroll then, rather than clip.
  scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  layout->addWidget(scrollArea_, 1);

  buttons_ = new QBoxLayout(QBoxLayout::LeftToRight);
  buttons_->setContentsMargins(6, 4, 6, 6);
  revealButton_ = makeButton(tr("Open Containing Folder"), this);
  editButton_ = makeButton(tr("Edit Properties..."), this);
  buttons_->addWidget(revealButton_);
  buttons_->addWidget(editButton_);
  layout->addLayout(buttons_);

  // The buttons are enabled only while a song is shown.
  connect(revealButton_, &QPushButton::clicked, this,
          [this]() { emit revealRequested(info_.songPk); });
  connect(editButton_, &QPushButton::clicked, this,
          [this]() { emit editPropertiesRequested(info_.songPk); });

  setInfo({});
}

void SongInfoPanel::setInfo(const SongInfo &info) {
  info_ = info;
  const bool hasSong = !info_.isEmpty();
  placeholder_->setVisible(!hasSong);
  scrollArea_->setVisible(hasSong);
  revealButton_->setEnabled(hasSong);
  editButton_->setEnabled(hasSong);
  // Replacing the widget deletes the old rows.
  auto *content = new QWidget;
  if (hasSong) {
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(6, 6, 6, 6);
    for (const SongInfoSection &section : info_.sections) {
      auto *title = new QLabel(section.title, content);
      QFont font = title->font();
      font.setBold(true);
      title->setFont(font);
      contentLayout->addWidget(title);
      auto *form = new QFormLayout;
      form->setRowWrapPolicy(QFormLayout::WrapLongRows);
      form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
      for (const SongInfoRow &row : section.rows) {
        form->addRow(row.label, makeValueLabel(row, content));
      }
      contentLayout->addLayout(form);
    }
    contentLayout->addStretch(1);
  }
  scrollArea_->setWidget(content);
}

const SongInfo &SongInfoPanel::info() const { return info_; }

void SongInfoPanel::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  // Side by side when both fit, else stacked.
  const QMargins margins = buttons_->contentsMargins();
  const int sideBySide = revealButton_->sizeHint().width() +
                         editButton_->sizeHint().width() + buttons_->spacing() +
                         margins.left() + margins.right();
  buttons_->setDirection(event->size().width() >= sideBySide
                             ? QBoxLayout::LeftToRight
                             : QBoxLayout::TopToBottom);
}
