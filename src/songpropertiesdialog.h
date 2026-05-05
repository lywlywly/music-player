#ifndef SONGPROPERTIESDIALOG_H
#define SONGPROPERTIESDIALOG_H

#include "columnregistry.h"
#include "songlibrary.h"
#include <QDialog>
#include <unordered_map>
#include <vector>

namespace Ui {
class SongPropertiesDialog;
}

class SongPropertiesDialog : public QDialog {
  Q_OBJECT

public:
  enum class RowSource { SongField, RemainingField, ComputedField };

  struct RowData {
    std::string rawKey;
    QString displayField;
    QString valueText;
    ColumnValueType valueType = ColumnValueType::Text;
    RowSource source = RowSource::SongField;
  };

  explicit SongPropertiesDialog(
      const MSong &song,
      const std::unordered_map<std::string, std::string> &remainingFields,
      const ColumnRegistry &columnRegistry, QWidget *parent = nullptr);
  ~SongPropertiesDialog();

  RowSource rowSourceAt(int row) const;
  int propertyRowCount() const;

private:
  static QString valueTypeToDisplayText(ColumnValueType valueType);
  void
  buildRows(const MSong &song,
            const std::unordered_map<std::string, std::string> &remainingFields,
            const ColumnRegistry &columnRegistry);
  void populateTable();

  Ui::SongPropertiesDialog *ui = nullptr;
  std::vector<RowData> rows_;
};

#endif // SONGPROPERTIESDIALOG_H
