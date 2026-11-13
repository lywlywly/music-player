#ifndef TABLESELECTIONSTYLE_H
#define TABLESELECTIONSTYLE_H

#include <QProxyStyle>

// Wraps the Qt "windows11" style (Qt 6.10+) to paint a selected row of a
// row-selecting QTableView as one band. The base style draws its accent bar
// in every cell (PE_PanelItemViewRow) and a separate rounded box per cell
// (CE_ItemViewItem). See "Table row selection" in docs/ui-styling.md.
class TableSelectionStyle : public QProxyStyle {
public:
  using QProxyStyle::QProxyStyle;

  void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                     QPainter *painter,
                     const QWidget *widget = nullptr) const override;
  void drawControl(ControlElement element, const QStyleOption *option,
                   QPainter *painter,
                   const QWidget *widget = nullptr) const override;
};

#endif // TABLESELECTIONSTYLE_H
