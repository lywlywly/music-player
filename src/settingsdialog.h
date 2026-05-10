#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "columndefinition.h"
#include "columnregistry.h"
#include "databasemanager.h"
#include "libraryexpression.h"
#include "playbackbackendmanager.h"
#include <QCheckBox>
#include <QColor>
#include <QDialog>
#include <QFontComboBox>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog {
  Q_OBJECT

public:
  explicit SettingsDialog(ColumnRegistry &columnRegistry,
                          DatabaseManager &databaseManager,
                          QWidget *parent = nullptr);
  ~SettingsDialog();

signals:
  void backendChanged(PlaybackBackendManager::Backend backend);
  void customFieldsChanged();
  void cloudUuidChanged(const QString &uuid);
  void lyricsFontChanged(const QString &fontFamily, int pointSize);
  void lyricsHighlightColorChanged(const QColor &color);

private:
  void refreshCustomFieldsList();
  void refreshComputedFieldsList();
  void addCustomFieldFromForm();
  void addComputedFieldFromForm();
  void removeSelectedCustomField();
  void removeSelectedComputedField();
  void updateCloudUuidStatus();
  void updateLyricsHighlightColorButton();
  void applySettings();
  ColumnDefinition buildCustomFieldDefinitionFromForm() const;
  ColumnDefinition buildComputedFieldDefinitionFromForm() const;
  bool expressionTypeMatchesValueType(const Expr &expr,
                                      ColumnValueType expectedType) const;
  Ui::SettingsDialog *ui;
  ColumnRegistry &columnRegistry_;
  DatabaseManager &databaseManager_;
  QList<ColumnDefinition> customFields_;
  QList<ColumnDefinition> computedFields_;
  QSet<QString> originalCustomFieldIds_;
  QSet<QString> originalComputedFieldIds_;
  QString initialCloudUuid_;
  bool initialCloudDisabledByUser_ = false;
  QString pendingCloudUuid_;
  bool pendingCloudDisabledByUser_ = false;
  QFontComboBox *lyricsFontFamilyCombo_ = nullptr;
  QSpinBox *lyricsFontSizeSpin_ = nullptr;
  QCheckBox *lyricsUseDefaultFontCheck_ = nullptr;
  QPushButton *lyricsHighlightColorButton_ = nullptr;
  QColor lyricsHighlightColor_ = QColor(0, 100, 255);
};

#endif // SETTINGSDIALOG_H
