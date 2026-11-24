#include "playstatssettingspage.h"

#include "cloudsyncsettings.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QUuid>
#include <QVBoxLayout>

PlayStatsSettingsPage::PlayStatsSettingsPage(QWidget *parent)
    : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  auto *group = new QGroupBox(tr("Cloud Sync User UUID"), this);
  auto *groupLayout = new QVBoxLayout(group);
  auto *note = new QLabel(tr("Keep this UUID secret. Anyone with it can read "
                             "and write your cloud play stats."),
                          group);
  note->setWordWrap(true);
  groupLayout->addWidget(note);
  auto *form = new QFormLayout();
  statusLabel_ = new QLabel(group);
  statusLabel_->setObjectName("cloud_uuid_status_label");
  form->addRow(tr("Status:"), statusLabel_);
  uuidEdit_ = new QLineEdit(group);
  uuidEdit_->setObjectName("cloud_uuid_edit");
  uuidEdit_->setReadOnly(true);
  uuidEdit_->setPlaceholderText("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
  form->addRow(tr("User UUID:"), uuidEdit_);
  groupLayout->addLayout(form);
  auto *buttons = new QHBoxLayout();
  buttons->addStretch();
  setUuidButton_ = new QPushButton(tr("Set UUID"), group);
  setUuidButton_->setObjectName("set_cloud_uuid_button");
  newUserButton_ = new QPushButton(tr("Start as New User"), group);
  newUserButton_->setObjectName("start_new_cloud_user_button");
  disableButton_ = new QPushButton(tr("Disable Cloud Sync"), group);
  disableButton_->setObjectName("disable_cloud_sync_button");
  buttons->addWidget(setUuidButton_);
  buttons->addWidget(newUserButton_);
  buttons->addWidget(disableButton_);
  groupLayout->addLayout(buttons);
  layout->addWidget(group);
  layout->addStretch();

  const QSettings settings;
  QString uuid =
      settings.value(CloudSyncSettings::kUserUuid).toString().trimmed();
  const bool disabledByUser =
      settings.value(CloudSyncSettings::kDisabledByUser, false).toBool();
  // A first-time user gets a UUID, saved on OK.
  if (uuid.isEmpty() && !disabledByUser) {
    uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  initialUuid_ = uuid;
  initialDisabledByUser_ = disabledByUser;
  pendingUuid_ = uuid;
  pendingDisabledByUser_ = disabledByUser;
  uuidEdit_->setText(pendingUuid_);
  updateStatus();

  connect(setUuidButton_, &QPushButton::clicked, this, [this]() {
    bool ok = false;
    const QString entered =
        QInputDialog::getText(this, tr("Set Cloud UUID"), tr("User UUID:"),
                              QLineEdit::Normal, QString(), &ok);
    if (!ok) {
      return;
    }
    const QString uuidText = entered.trimmed();
    if (!uuidText.isEmpty() && !CloudSyncSettings::isValidUuid(uuidText)) {
      QMessageBox::warning(this, tr("Set Cloud UUID"),
                           tr("Invalid UUID format."));
      return;
    }
    setPendingUuid(uuidText);
    QMessageBox::information(
        this, tr("Set Cloud UUID"),
        tr("Cloud UUID will be saved when you click OK. Keep it secret."));
  });
  connect(newUserButton_, &QPushButton::clicked, this, [this]() {
    setPendingUuid(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QMessageBox::information(this, tr("Start as New User"),
                             tr("A new cloud UUID was generated. It will be "
                                "saved when you click OK."));
  });
  connect(disableButton_, &QPushButton::clicked, this, [this]() {
    setPendingUuid({});
    QMessageBox::information(this, tr("Disable Cloud Sync"),
                             tr("Cloud sync will be disabled when you click "
                                "OK. Local play stats are kept."));
  });
}

QWidget *PlayStatsSettingsPage::widget() { return this; }

void PlayStatsSettingsPage::setPendingUuid(const QString &uuid) {
  pendingUuid_ = uuid;
  pendingDisabledByUser_ = uuid.isEmpty();
  uuidEdit_->setText(pendingUuid_);
  updateStatus();
}

void PlayStatsSettingsPage::updateStatus() {
  const QString uuid = pendingUuid_.trimmed();
  const bool valid = CloudSyncSettings::isValidUuid(uuid);
  // Invalid: a stored value edited by hand, which sync skips.
  statusLabel_->setText(valid            ? tr("Enabled")
                        : uuid.isEmpty() ? tr("Disabled")
                                         : tr("Invalid UUID"));
  setUuidButton_->setVisible(!valid);
  newUserButton_->setVisible(!valid);
  disableButton_->setVisible(valid);
}

void PlayStatsSettingsPage::apply() {
  const QString uuid = pendingUuid_.trimmed();
  QSettings settings;
  settings.setValue(CloudSyncSettings::kUserUuid, uuid);
  settings.setValue(CloudSyncSettings::kDisabledByUser, pendingDisabledByUser_);
  if (uuid.isEmpty()) {
    settings.setValue(CloudSyncSettings::kRebasePending, false);
  } else if (uuid != initialUuid_ ||
             pendingDisabledByUser_ != initialDisabledByUser_) {
    settings.setValue(CloudSyncSettings::kLastSyncedAt, 0);
    settings.setValue(CloudSyncSettings::kRebasePending, true);
  }
  if (uuid != initialUuid_) {
    emit uuidChanged(uuid);
  }
}
