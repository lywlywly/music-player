#ifndef FIELDEDITDIALOG_H
#define FIELDEDITDIALOG_H

#include <QDialog>

namespace Ui {
class FieldEditDialog;
}

class FieldEditDialog : public QDialog {
  Q_OBJECT

public:
  explicit FieldEditDialog(const QString &fieldLabel, const QString &typeLabel,
                           const QString &valueText, QWidget *parent = nullptr);
  ~FieldEditDialog();

  QString editedValue() const;

private:
  Ui::FieldEditDialog *ui = nullptr;
};

#endif // FIELDEDITDIALOG_H
