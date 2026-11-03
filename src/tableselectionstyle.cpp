#include "tableselectionstyle.h"

#include <QGuiApplication>
#include <QPainter>
#include <QStyleHints>
#include <QStyleOptionViewItem>
#include <QTableView>

namespace {

// The base style's rounding radius for item-view selections.
constexpr int kRadius = 4;
// Opacity of the gray selection band: white over dark themes, black over
// light ones. The base style's subtle gray is 6% / 4%.
constexpr float kFillAlpha = 0.14f;

// The selected table cell to repaint, or nullptr to leave it to the base style.
// High contrast (reported as an Unknown color scheme) is left alone.
const QStyleOptionViewItem *selectedRowCell(const QStyleOption *option,
                                            const QWidget *widget) {
  const auto *vopt = qstyleoption_cast<const QStyleOptionViewItem *>(option);
  const auto *table = qobject_cast<const QTableView *>(widget);
  if (!vopt || !(vopt->state & QStyle::State_Selected) || !table ||
      table->selectionBehavior() != QAbstractItemView::SelectRows ||
      QGuiApplication::styleHints()->colorScheme() ==
          Qt::ColorScheme::Unknown) {
    return nullptr;
  }
  return vopt;
}

// Paints this cell's part of the row band, rounded only at the row's ends,
// with the base style's margins and a gray stronger than its subtle fill.
void paintBand(QPainter *painter, const QStyleOptionViewItem &option) {
  using Pos = QStyleOptionViewItem::ViewItemPosition;
  Pos pos = option.viewItemPosition;
  // QTableView counts positions in visual order, which is mirrored in RTL.
  if (option.direction == Qt::RightToLeft) {
    pos = pos == Pos::Beginning ? Pos::End
          : pos == Pos::End     ? Pos::Beginning
                                : pos;
  }

  const bool light =
      QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light;
  QColor fill = light ? Qt::black : Qt::white;
  fill.setAlphaF(kFillAlpha);

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);
  painter->setPen(Qt::NoPen);
  painter->setBrush(fill);
  painter->setClipRect(option.rect);
  // A negative margin pushes the rounded corner past the clip, leaving a
  // square edge where the neighbouring cell continues the band.
  const QRect &r = option.rect;
  switch (pos) {
  case Pos::Beginning:
    painter->drawRoundedRect(r.marginsRemoved({2, 2, -kRadius, 2}), kRadius,
                             kRadius);
    break;
  case Pos::Middle:
    painter->drawRect(r.marginsRemoved({0, 2, 0, 2}));
    break;
  case Pos::End:
    painter->drawRoundedRect(r.marginsRemoved({-kRadius, 2, 2, 2}), kRadius,
                             kRadius);
    break;
  default:
    painter->drawRoundedRect(r.marginsRemoved({2, 2, 2, 2}), kRadius, kRadius);
    break;
  }
  painter->restore();
}

} // namespace

void TableSelectionStyle::drawPrimitive(PrimitiveElement element,
                                        const QStyleOption *option,
                                        QPainter *painter,
                                        const QWidget *widget) const {
  const QStyleOptionViewItem *vopt = element == PE_PanelItemViewRow
                                         ? selectedRowCell(option, widget)
                                         : nullptr;
  if (!vopt) {
    QProxyStyle::drawPrimitive(element, option, painter, widget);
    return;
  }
  // Keeps the alternate-row background but not the per-cell accent bar.
  QStyleOptionViewItem unselected(*vopt);
  unselected.state &= ~State_Selected;
  QProxyStyle::drawPrimitive(element, &unselected, painter, widget);
}

void TableSelectionStyle::drawControl(ControlElement element,
                                      const QStyleOption *option,
                                      QPainter *painter,
                                      const QWidget *widget) const {
  const QStyleOptionViewItem *vopt =
      element == CE_ItemViewItem ? selectedRowCell(option, widget) : nullptr;
  if (!vopt) {
    QProxyStyle::drawControl(element, option, painter, widget);
    return;
  }
  // Cell background, then the band, then the content drawn as unselected.
  proxy()->drawPrimitive(PE_PanelItemViewItem, vopt, painter, widget);
  paintBand(painter, *vopt);
  QStyleOptionViewItem content(*vopt);
  content.state &= ~(State_Selected | State_MouseOver);
  content.backgroundBrush = Qt::NoBrush;
  QProxyStyle::drawControl(element, &content, painter, widget);
}
