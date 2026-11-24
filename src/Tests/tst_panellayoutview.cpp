#include <QLabel>
#include <QPointer>
#include <QSignalSpy>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>

#include "../layout/panellayoutview.h"

namespace {
using Kind = PanelLayout::Kind;
using Node = PanelLayout::Node;

Node element(const QString &id) {
  return {.kind = Kind::Element, .elementId = id};
}

Node splitter(Node first, Node second) {
  Node node{.kind = Kind::HSplitter, .sizes = {1, 1}};
  node.children = {std::move(first), std::move(second)};
  return node;
}

Node tabs(std::vector<Node> children) {
  return {.kind = Kind::Tabs,
          .tabTitles = std::vector<QString>(children.size()),
          .children = std::move(children)};
}

QTabWidget *tabWidgetOf(QWidget *widget) {
  for (; widget != nullptr; widget = widget->parentWidget()) {
    if (auto *tabWidget = qobject_cast<QTabWidget *>(widget)) {
      return tabWidget;
    }
  }
  return nullptr;
}
} // namespace

class TestPanelLayoutView : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void setLayout_placesElementsAndLabels();
  void box_placesChildrenAndKeepsARowsHeight();
  void setLayout_again_keepsElementsAndParksTheOthers();
  void splitterMoveAndTabSwitch_areWrittenToTheTree();
  void pathOf_findsTheNodeOfAnyWidgetInIt();

private:
  // [a | tabs(b, placeholder)], with c registered but not placed.
  PanelLayout sample() const {
    return PanelLayout(splitter(element("a"), tabs({element("b"), Node{}})));
  }

  QWidget *window_ = nullptr;
  QWidget *store_ = nullptr;
  PanelLayoutView *view_ = nullptr;
  QPointer<QWidget> a_, b_, c_;
};

void TestPanelLayoutView::init() {
  window_ = new QWidget;
  auto *host = new QWidget(window_);
  host->setObjectName("host");
  store_ = new QWidget(window_);
  store_->hide();
  view_ = new PanelLayoutView(host, store_, "Empty slot");
  a_ = new QWidget;
  b_ = new QWidget;
  c_ = new QWidget;
  view_->addElement("a", "A", a_);
  view_->addElement("b", "B", b_);
  view_->addElement("c", "C", c_);
  window_->resize(400, 300);
  window_->show();
  host->resize(400, 300);
}

void TestPanelLayoutView::cleanup() {
  delete window_;
  window_ = nullptr;
}

void TestPanelLayoutView::setLayout_placesElementsAndLabels() {
  view_->setUnavailableTitle("gone", "Gone Plugin");
  PanelLayout layout = sample();
  layout.split({0}, Kind::VSplitter);
  // An unavailable element and one with no name either.
  layout.replaceWithElement({0, 1}, "gone");
  layout.addChild({1});
  layout.replaceWithElement({1, 2}, "unknown");
  view_->setLayout(layout);

  auto *root = qobject_cast<QSplitter *>(view_->widgetAt({}));
  QVERIFY(root != nullptr);
  QCOMPARE(root->parentWidget()->objectName(), QString("host"));
  QVERIFY(a_->isVisible());
  QTabWidget *tabWidget = tabWidgetOf(b_);
  QVERIFY(tabWidget != nullptr);
  QCOMPARE(tabWidget->count(), 3);
  QCOMPARE(tabWidget->tabText(0), QString("B"));
  QCOMPARE(tabWidget->tabText(1), QString("Empty"));
  QCOMPARE(tabWidget->tabText(2), QString("unknown"));
  QVERIFY(b_->isVisible());
  // A placeholder shows the text the view was given.
  QCOMPARE(qobject_cast<QLabel *>(tabWidget->widget(1))->text(),
           QString("Empty slot"));
  QCOMPARE(qobject_cast<QLabel *>(tabWidget->widget(2))->text(),
           QString("unknown is not available"));
  QCOMPARE(qobject_cast<QLabel *>(view_->widgetAt({0, 1}))->text(),
           QString("Gone Plugin is not available"));
  // c is not in the tree.
  QCOMPARE(c_->parentWidget(), store_);
  QVERIFY(!c_->isVisible());

  // A container's tab is named by the elements in it.
  layout.wrapInTabs({0});
  layout.split({1, 0}, Kind::HSplitter);
  layout.replaceWithElement({1, 0, 1}, "c");
  view_->setLayout(layout);
  QCOMPARE(tabWidgetOf(b_)->tabText(0), QString("B / C"));
  // A named tab shows its name.
  layout.setTabTitle({1, 0}, "Named");
  view_->setLayout(layout);
  QCOMPARE(tabWidgetOf(b_)->tabText(0), QString("Named"));
}

void TestPanelLayoutView::box_placesChildrenAndKeepsARowsHeight() {
  // vbox(hbox(c), a): each child placed in its box, in order, shown. The row
  // keeps the height of its fixed-height child, though a (Preferred, like the
  // row's own widget) does not expand either.
  c_->setFixedHeight(20);
  Node row{.kind = Kind::HBox, .children = {element("c")}};
  view_->setLayout(PanelLayout(
      {.kind = Kind::VBox, .children = {std::move(row), element("a")}}));
  QWidget *rowWidget = view_->widgetAt({0});
  QCOMPARE(c_->parentWidget(), rowWidget);
  QCOMPARE(rowWidget->parentWidget(), view_->widgetAt({}));
  QCOMPARE(a_->parentWidget(), view_->widgetAt({}));
  QVERIFY(c_->isVisible() && a_->isVisible());
  QTRY_COMPARE(rowWidget->height(), 20);
  QVERIFY(rowWidget->y() < a_->y());
  QVERIFY(a_->height() > 200);

  // Deleting the boxes on a rebuild keeps c, parked in the store.
  view_->setLayout(sample());
  QVERIFY(c_);
  QCOMPARE(c_->parentWidget(), store_);
}

void TestPanelLayoutView::setLayout_again_keepsElementsAndParksTheOthers() {
  view_->setLayout(sample());
  // Only b, as the root.
  view_->setLayout(PanelLayout(element("b")));
  QVERIFY(a_ && b_ && c_);
  QCOMPARE(b_->parentWidget()->objectName(), QString("host"));
  QVERIFY(b_->isVisible());
  QCOMPARE(a_->parentWidget(), store_);
  QVERIFY(a_->isHidden());

  // An element as the root goes back to the store, not deleted.
  view_->setLayout(sample());
  QVERIFY(b_);
  QVERIFY(tabWidgetOf(b_) != nullptr);
  QVERIFY(a_->isVisible());
}

void TestPanelLayoutView::splitterMoveAndTabSwitch_areWrittenToTheTree() {
  QSignalSpy spy(view_, &PanelLayoutView::layoutChanged);
  view_->setLayout(sample());

  tabWidgetOf(b_)->setCurrentIndex(1);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(view_->layout().node({1}).currentTab, 1);

  auto *root = qobject_cast<QSplitter *>(view_->widgetAt({}));
  root->setSizes({100, 300});
  emit root->splitterMoved(100, 1);
  QCOMPARE(spy.count(), 2);
  const QList<int> sizes = root->sizes();
  QCOMPARE(sizes.at(0), 100);
  QCOMPARE(view_->layout().node({}).sizes,
           (std::vector<int>{sizes.at(0), sizes.at(1)}));

  // A rebuild applies the tree and reports nothing: taking b out of the old
  // tab widget and adding the first page to the new one would otherwise
  // write a stale index.
  view_->setLayout(view_->layout());
  QCOMPARE(spy.count(), 2);
  QCOMPARE(tabWidgetOf(b_)->currentIndex(), 1);
  QCOMPARE(view_->layout().node({1}).currentTab, 1);
}

void TestPanelLayoutView::pathOf_findsTheNodeOfAnyWidgetInIt() {
  view_->setLayout(sample());
  auto *child = new QLabel(a_);
  QVERIFY(view_->pathOf(child) == PanelLayout::Path{0});
  QVERIFY(view_->pathOf(b_) == (PanelLayout::Path{1, 0}));
  auto *root = qobject_cast<QSplitter *>(view_->widgetAt({}));
  QVERIFY(view_->pathOf(root->handle(1)) == PanelLayout::Path{});
  QVERIFY(view_->pathOf(tabWidgetOf(b_)->tabBar()) == PanelLayout::Path{1});
  // Outside the layout: an element in the store, and the window.
  QVERIFY(!view_->pathOf(c_).has_value());
  QVERIFY(!view_->pathOf(window_).has_value());
}

QTEST_MAIN(TestPanelLayoutView)
#include "tst_panellayoutview.moc"
