#include <QTest>

#include "../layout/panellayout.h"

namespace {
using Kind = PanelLayout::Kind;
using Node = PanelLayout::Node;

Node element(const QString &id) {
  return {.kind = Kind::Element, .elementId = id};
}

Node splitter(Kind kind, Node first, Node second,
              std::vector<int> sizes = {1, 1}) {
  Node node{.kind = kind, .sizes = std::move(sizes)};
  node.children = {std::move(first), std::move(second)};
  return node;
}

Node tabs(std::vector<Node> children, int current = 0) {
  return {.kind = Kind::Tabs,
          .currentTab = current,
          .tabTitles = std::vector<QString>(children.size()),
          .children = std::move(children)};
}

// [lyrics | properties] above cover, beside playlist.
PanelLayout sample() {
  return PanelLayout(
      splitter(Kind::HSplitter,
               splitter(Kind::VSplitter,
                        tabs({element("lyrics"), element("properties")}, 1),
                        element("cover"), {300, 500}),
               element("playlist"), {320, 960}));
}
} // namespace

class TestPanelLayout : public QObject {
  Q_OBJECT

private slots:
  void json_roundTrip();
  void fromJson_rejectsInvalidTrees_data();
  void fromJson_rejectsInvalidTrees();
  void fromJson_repeatedElementBecomesPlaceholder();
  void replaceWithElement_replacesTheNodeAndItsSubtree();
  void splitAndWrapInTabs_keepTheNode();
  void take_leavesAPlaceholder();
  void paste_elementPlacedSinceBecomesPlaceholder();
  void tabs_addMoveAndRemove();
  void tabTitles_stayWithTheirTabs();
};

void TestPanelLayout::json_roundTrip() {
  const PanelLayout layout = sample();
  const std::optional<PanelLayout> read =
      PanelLayout::fromJson(layout.toJson());
  QVERIFY(read.has_value());
  QVERIFY(*read == layout);
  QCOMPARE(layout.placedElements(),
           (QStringList{"lyrics", "properties", "cover", "playlist"}));

  // Keys in sorted order, as QJsonDocument writes them.
  const QByteArray json = R"({"children":[{"type":"placeholder"},)"
                          R"({"id":"spectrum","type":"element"}],)"
                          R"("sizes":[2,3],"type":"vsplitter"})";
  const std::optional<PanelLayout> hand = PanelLayout::fromJson(json);
  QVERIFY(hand.has_value());
  QVERIFY(*hand == PanelLayout(splitter(Kind::VSplitter, Node{},
                                        element("spectrum"), {2, 3})));
  QCOMPARE(hand->toJson(), json);
}

void TestPanelLayout::fromJson_rejectsInvalidTrees_data() {
  QTest::addColumn<QByteArray>("json");
  QTest::newRow("not json") << QByteArray("{");
  QTest::newRow("array") << QByteArray("[]");
  QTest::newRow("unknown type") << QByteArray(R"({"type":"box"})");
  QTest::newRow("splitter with one child")
      << QByteArray(R"({"type":"hsplitter","sizes":[1,1],)"
                    R"("children":[{"type":"placeholder"}]})");
  QTest::newRow("splitter without sizes")
      << QByteArray(R"({"type":"hsplitter","children":)"
                    R"([{"type":"placeholder"},{"type":"placeholder"}]})");
  QTest::newRow("negative size")
      << QByteArray(R"({"type":"hsplitter","sizes":[-1,1],"children":)"
                    R"([{"type":"placeholder"},{"type":"placeholder"}]})");
  QTest::newRow("size not a number")
      << QByteArray(R"({"type":"hsplitter","sizes":["1",1],"children":)"
                    R"([{"type":"placeholder"},{"type":"placeholder"}]})");
  QTest::newRow("tabs without children")
      << QByteArray(R"({"type":"tabs","current":0})");
  QTest::newRow("current out of range")
      << QByteArray(R"({"type":"tabs","current":1,)"
                    R"("children":[{"type":"placeholder"}]})");
  QTest::newRow("element without id") << QByteArray(R"({"type":"element"})");
  QTest::newRow("element with children")
      << QByteArray(R"({"type":"element","id":"a",)"
                    R"("children":[{"type":"placeholder"}]})");
  QTest::newRow("placeholder with children")
      << QByteArray(R"({"type":"placeholder",)"
                    R"("children":[{"type":"placeholder"}]})");
  QTest::newRow("titles not one per tab")
      << QByteArray(R"({"type":"tabs","current":0,"titles":["a","b"],)"
                    R"("children":[{"type":"placeholder"}]})");
  QTest::newRow("title not a string")
      << QByteArray(R"({"type":"tabs","current":0,"titles":[1],)"
                    R"("children":[{"type":"placeholder"}]})");
  QTest::newRow("invalid child")
      << QByteArray(R"({"type":"tabs","current":0,"children":[{"type":"x"}]})");
}

void TestPanelLayout::fromJson_rejectsInvalidTrees() {
  QFETCH(QByteArray, json);
  QVERIFY(!PanelLayout::fromJson(json).has_value());
}

void TestPanelLayout::fromJson_repeatedElementBecomesPlaceholder() {
  const PanelLayout layout(
      splitter(Kind::HSplitter, element("cover"),
               tabs({element("lyrics"), element("cover")})));
  const std::optional<PanelLayout> read =
      PanelLayout::fromJson(layout.toJson());
  QVERIFY(read.has_value());
  // The first one, in tree order, is kept.
  QVERIFY(*read == PanelLayout(splitter(Kind::HSplitter, element("cover"),
                                        tabs({element("lyrics"), Node{}}))));
}

void TestPanelLayout::replaceWithElement_replacesTheNodeAndItsSubtree() {
  PanelLayout layout = sample();
  // An element.
  layout.replaceWithElement({1}, "spectrum");
  QCOMPARE(layout.node({1}), element("spectrum"));
  // A container: its elements leave the tree.
  layout.replaceWithElement({0}, "playlist");
  QCOMPARE(layout.placedElements(), (QStringList{"playlist", "spectrum"}));
  // A placeholder.
  layout.take({1});
  layout.replaceWithElement({1}, "cover");
  QCOMPARE(layout.node({1}), element("cover"));
}

void TestPanelLayout::splitAndWrapInTabs_keepTheNode() {
  PanelLayout layout = sample();
  layout.split({1}, Kind::VSplitter);
  QCOMPARE(layout.node({1}),
           splitter(Kind::VSplitter, element("playlist"), Node{}));

  // A placeholder splits into two placeholders.
  layout.split({1, 1}, Kind::HSplitter);
  QCOMPARE(layout.node({1, 1}), splitter(Kind::HSplitter, Node{}, Node{}));

  layout.wrapInTabs({0, 1});
  QCOMPARE(layout.node({0, 1}), tabs({element("cover")}));
}

void TestPanelLayout::take_leavesAPlaceholder() {
  PanelLayout layout = sample();
  const Node taken = layout.take({0, 0});
  QCOMPARE(taken, tabs({element("lyrics"), element("properties")}, 1));
  QCOMPARE(layout.node({0, 0}), Node{});
  QCOMPARE(layout.placedElements(), (QStringList{"cover", "playlist"}));
}

void TestPanelLayout::paste_elementPlacedSinceBecomesPlaceholder() {
  PanelLayout layout = sample();
  Node cut = layout.take({0, 0});
  // Lyrics is inserted elsewhere before the paste.
  layout.replaceWithElement({0, 0}, "lyrics");
  layout.paste({1}, std::move(cut));
  QCOMPARE(layout.node({1}), tabs({Node{}, element("properties")}, 1));
  QCOMPARE(layout.placedElements(),
           (QStringList{"lyrics", "cover", "properties"}));

  // Pasting over a node that holds one of the pasted elements keeps it.
  Node again = layout.take({1});
  layout.replaceWithElement({1}, "properties");
  layout.paste({1}, std::move(again));
  QCOMPARE(layout.node({1}), tabs({Node{}, element("properties")}, 1));
}

void TestPanelLayout::tabs_addMoveAndRemove() {
  PanelLayout layout = sample();
  const PanelLayout::Path tabsPath{0, 0};
  layout.addTab(tabsPath);
  QCOMPARE(layout.node(tabsPath),
           tabs({element("lyrics"), element("properties"), Node{}}, 1));

  // The current tab follows its page.
  layout.moveTab({0, 0, 1}, -1);
  QCOMPARE(layout.node(tabsPath),
           tabs({element("properties"), element("lyrics"), Node{}}, 0));
  layout.moveTab({0, 0, 1}, 1);
  QCOMPARE(layout.node(tabsPath),
           tabs({element("properties"), Node{}, element("lyrics")}, 0));
  layout.moveTab({0, 0, 1}, -1);
  QCOMPARE(layout.node(tabsPath),
           tabs({Node{}, element("properties"), element("lyrics")}, 1));

  // Removing the current tab makes the next one current; removing the last
  // one, the one before it.
  layout.removeTab({0, 0, 1});
  QCOMPARE(layout.node(tabsPath), tabs({Node{}, element("lyrics")}, 1));
  layout.removeTab({0, 0, 1});
  QCOMPARE(layout.node(tabsPath), tabs({Node{}}, 0));
  // Removing a tab before the current one keeps the current page.
  PanelLayout other = sample();
  other.removeTab({0, 0, 0});
  QCOMPARE(other.node(tabsPath), tabs({element("properties")}, 0));
}

void TestPanelLayout::tabTitles_stayWithTheirTabs() {
  PanelLayout layout = sample();
  const PanelLayout::Path tabsPath{0, 0};
  const auto titles = [&]() { return layout.node(tabsPath).tabTitles; };
  // No names: left out of the JSON.
  QVERIFY(!layout.toJson().contains("titles"));

  layout.setTabTitle({0, 0, 0}, "Words");
  QCOMPARE(titles(), (std::vector<QString>{"Words", ""}));
  // A name belongs to the tab, not to what is in it.
  layout.split({0, 0, 0}, Kind::VSplitter);
  layout.take({0, 0, 0});
  QCOMPARE(titles(), (std::vector<QString>{"Words", ""}));
  layout.addTab(tabsPath);
  QCOMPARE(titles(), (std::vector<QString>{"Words", "", ""}));
  layout.moveTab({0, 0, 0}, 1);
  QCOMPARE(titles(), (std::vector<QString>{"", "Words", ""}));
  layout.removeTab({0, 0, 0});
  QCOMPARE(titles(), (std::vector<QString>{"Words", ""}));

  // Kept in the JSON.
  QVERIFY(layout.toJson().contains(R"("titles":["Words",""])"));
  const std::optional<PanelLayout> read =
      PanelLayout::fromJson(layout.toJson());
  QVERIFY(read.has_value());
  QVERIFY(*read == layout);

  layout.setTabTitle({0, 0, 0}, QString());
  QVERIFY(!layout.toJson().contains("titles"));
}

QTEST_APPLESS_MAIN(TestPanelLayout)
#include "tst_panellayout.moc"
