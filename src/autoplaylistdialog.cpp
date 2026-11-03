#include "autoplaylistdialog.h"

#include "monitoredlibrary.h"
#include "songlibrary.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

AutoPlaylistDialog::AutoPlaylistDialog(const SongLibrary &library,
                                       const MonitoredLibrary &monitoredLibrary,
                                       const QString &name,
                                       const QString &expression,
                                       QWidget *parent)
    : QDialog(parent), library_(library), monitoredLibrary_(monitoredLibrary) {
  setWindowTitle(tr("Autoplaylist"));
  nameEdit_ = new QLineEdit(name, this);
  nameEdit_->setObjectName("name_edit");
  expressionEdit_ = new QLineEdit(expression, this);
  expressionEdit_->setObjectName("expression_edit");
  expressionEdit_->setPlaceholderText(
      QStringLiteral("genre HAS jazz AND play_count > 0"));
  expressionEdit_->setMinimumWidth(420);
  statusLabel_ = new QLabel(this);
  statusLabel_->setObjectName("status_label");
  statusLabel_->setWordWrap(true);
  buttons_ = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

  auto *form = new QFormLayout();
  form->addRow(tr("Name:"), nameEdit_);
  form->addRow(tr("Expression:"), expressionEdit_);
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(statusLabel_);
  layout->addWidget(buttons_);

  connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(nameEdit_, &QLineEdit::textChanged, this,
          &AutoPlaylistDialog::updateStatus);
  connect(expressionEdit_, &QLineEdit::textChanged, this,
          &AutoPlaylistDialog::updateStatus);
  expressionEdit_->setFocus();
  updateStatus();
}

QString AutoPlaylistDialog::name() const { return nameEdit_->text().trimmed(); }

QString AutoPlaylistDialog::expression() const {
  return expressionEdit_->text();
}

void AutoPlaylistDialog::updateStatus() {
  const ExprParseResult parsed = parseSongFilter(library_, expression());
  if (parsed.ok()) {
    const std::vector<int> songPks = monitoredLibrary_.songPks();
    const auto count =
        std::count_if(songPks.begin(), songPks.end(), [&](int songPk) {
          return library_.matches(*parsed.expr, songPk);
        });
    statusLabel_->setStyleSheet(QString());
    statusLabel_->setText(count == 1 ? tr("1 matching song")
                                     : tr("%1 matching songs").arg(count));
  } else {
    statusLabel_->setStyleSheet(QStringLiteral("color: #b42318;"));
    statusLabel_->setText(parsed.error.text());
  }
  buttons_->button(QDialogButtonBox::Ok)
      ->setEnabled(parsed.ok() && !name().isEmpty());
}
