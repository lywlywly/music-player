#include "addfielddialog.h"
#include "ui_addfielddialog.h"

AddFieldDialog::AddFieldDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::AddFieldDialog) {
  ui->setupUi(this);
  ui->value_edit->setPlaceholderText(
      QStringLiteral("Use ';' to separate multiple values"));
}

AddFieldDialog::~AddFieldDialog() { delete ui; }

QString AddFieldDialog::fieldName() const { return ui->name_edit->text(); }

QString AddFieldDialog::fieldValue() const {
  return ui->value_edit->toPlainText();
}
