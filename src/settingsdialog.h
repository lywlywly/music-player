#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "columndefinition.h"
#include "columnregistry.h"
#include "databasemanager.h"
#include "libraryexpression.h"
#include "playbackbackendmanager.h"
#include "statusruntimesymboltable.h"
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QFontComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <unordered_map>

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
  void setDisplayExpressionPreviewContext(
      const std::unordered_map<std::string, FieldValue> *song,
      const StatusRuntimeSymbolTable &runtimeSymbols);

signals:
  void backendChanged(PlaybackBackendManager::Backend backend);
  void customFieldsChanged();
  void cloudUuidChanged(const QString &uuid);
  void lyricsFontChanged(const QString &fontFamily, int pointSize);
  void lyricsHighlightColorChanged(const QColor &color);
  void statusBarExpressionChanged(const QString &expression);
  void windowTitleExpressionChanged(const QString &expression);
  void displayThemeModeChanged(const QString &mode);

private:
  void refreshCustomFieldsList();
  void refreshComputedFieldsList();
  void addCustomFieldFromForm();
  void addComputedFieldFromForm();
  void removeSelectedCustomField();
  void removeSelectedComputedField();
  void updateCloudUuidStatus();
  void updateLyricsHighlightColorButton();
  void setActiveDisplayExpressionEditor(QPlainTextEdit *editor);
  void updateActiveDisplayExpressionPreview();
  QString
  evaluateDisplayExpressionPreview(const QString &expressionText,
                                   const QString &fallbackExpression) const;
  void applySettings();
  ColumnDefinition buildCustomFieldDefinitionFromForm() const;
  ColumnDefinition buildComputedFieldDefinitionFromForm() const;
  FieldDefinition buildCustomFieldSchemaFromForm() const;
  FieldDefinition buildComputedFieldSchemaFromForm() const;
  bool expressionTypeMatchesValueType(const Expr &expr,
                                      ValueType expectedType) const;
  Ui::SettingsDialog *ui;
  ColumnRegistry &columnRegistry_;
  DatabaseManager &databaseManager_;
  QList<ColumnDefinition> customFields_;
  QList<ColumnDefinition> computedFields_;
  QHash<QString, FieldDefinition> customFieldSchemasById_;
  QHash<QString, FieldDefinition> computedFieldSchemasById_;
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
  QComboBox *displayThemeModeCombo_ = nullptr;
  QPlainTextEdit *statusExpressionEdit_ = nullptr;
  QPlainTextEdit *windowTitleExpressionEdit_ = nullptr;
  QLabel *activeDisplayExpressionPreviewLabel_ = nullptr;
  QPlainTextEdit *activeDisplayExpressionEdit_ = nullptr;
  QPushButton *resetDisplayExpressionsButton_ = nullptr;
  QColor lyricsHighlightColor_ = QColor(0, 100, 255);
  std::unordered_map<std::string, FieldValue> previewSong_;
  StatusRuntimeSymbolTable previewRuntimeSymbols_;
};

#endif // SETTINGSDIALOG_H
