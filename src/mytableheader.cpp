#include "mytableheader.h"

#include <QContextMenuEvent>

#include "qmenu.h"
#include "songtablemodel.h"

MyTableHeader::MyTableHeader(Qt::Orientation orientation, QWidget* parent)
    : QHeaderView(orientation, parent),
      isShown{std::vector<bool>(fieldToStringMap.size(), true)} {}

void MyTableHeader::contextMenuEvent(QContextMenuEvent* event) {
  QMenu menu;

  QList<QString> labels;
  std::transform(fieldToStringMap.begin(), fieldToStringMap.end(),
                 std::inserter(labels, labels.begin()),
                 [](const auto& pair) { return pair.second; });
  for (int i = 0; i < labels.size(); ++i) {
    setSectionHidden(i, !isShown[i]);
    QAction* hideAction = menu.addAction(labels[i]);
    hideAction->setCheckable(true);
    hideAction->setChecked(isShown[i]);
    connect(hideAction, &QAction::triggered, this, [this, i]() {
      if (isShown[i]) {
        isShown[i] = false;
      } else {
        isShown[i] = true;
      }
      setSectionHidden(i, !isShown[i]);
    });
  }
  menu.exec(event->globalPos());
}
