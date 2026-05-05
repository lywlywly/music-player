#include "fieldeditdialog.h"
#include "ui_fieldeditdialog.h"

FieldEditDialog::FieldEditDialog(const QString &fieldLabel,
                                 const QString &typeLabel,
                                 const QString &valueText, QWidget *parent)
    : QDialog(parent), ui(new Ui::FieldEditDialog) {
  ui->setupUi(this);
  ui->field_label_value->setText(fieldLabel);
  ui->type_label_value->setText(typeLabel);
  ui->value_edit->setPlainText(valueText);
}

FieldEditDialog::~FieldEditDialog() { delete ui; }

QString FieldEditDialog::editedValue() const {
  return ui->value_edit->toPlainText();
}
