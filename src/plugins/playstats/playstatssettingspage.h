#ifndef PLAYSTATSSETTINGSPAGE_H
#define PLAYSTATSSETTINGSPAGE_H

#include "isettingspageprovider.h"
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

// The Play Stats page of the Settings dialog: the cloud sync user UUID,
// saved as the playstats/* settings keys (docs/cloud-sync.md). The UUID is
// read-only and changes only through the page's buttons, which refuse an
// invalid one.
class PlayStatsSettingsPage : public QWidget, public IPluginSettingsPage {
  Q_OBJECT

public:
  explicit PlayStatsSettingsPage(QWidget *parent = nullptr);

  QWidget *widget() override;
  // Saves the UUID; a new one is to be rebased (rebase_pending), and sends
  // uuidChanged.
  void apply() override;

signals:
  // apply() saved another UUID; empty when sync was disabled.
  void uuidChanged(const QString &uuid);

private:
  void setPendingUuid(const QString &uuid);
  void updateStatus();

  QString initialUuid_;
  bool initialDisabledByUser_ = false;
  QString pendingUuid_;
  bool pendingDisabledByUser_ = false;
  QLabel *statusLabel_ = nullptr;
  QLineEdit *uuidEdit_ = nullptr;
  QPushButton *setUuidButton_ = nullptr;
  QPushButton *newUserButton_ = nullptr;
  QPushButton *disableButton_ = nullptr;
};

#endif // PLAYSTATSSETTINGSPAGE_H
