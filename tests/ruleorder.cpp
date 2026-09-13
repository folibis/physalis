#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "RulesPanel.h"
#include "SceneSerializer.h"

#include <QApplication>
#include <QTabWidget>
#include <QCheckBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QFrame>
#include <cstdio>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 12; ++i)
        QCoreApplication::processEvents();
}

Rule ruleNamed(const char *name)
{
    Rule r;
    r.name = QString::fromLatin1(name);
    r.subjectName = QStringLiteral("body_1");
    r.conditionKey = QStringLiteral("positionY");
    r.compare = Rule::Compare::Less;
    r.conditionValue = 0.0;
    r.targetName = QStringLiteral("body_1");
    r.propertyKey = QStringLiteral("gravityScale");
    r.op = Rule::Op::Set;
    r.value = 1.0;
    return r;
}

QStringList orderOf(CanvasScene *scene)
{
    QStringList names;
    for (const Rule &r : scene->rules())
        names << r.name;
    return names;
}

QList<QToolButton *> buttons(RulesPanel *panel, const char *name)
{
    return panel->findChildren<QToolButton *>(QString::fromLatin1(name));
}

} // namespace

// Rules are applied in the order they are listed, so the order is part of what
// the scene means and moving one is an edit like any other. Up and down
// buttons rather than a drag: the card is a form full of controls, with
// nowhere to grab that does not already belong to one of them.
TEST(RuleOrder, Behaves)
{
    MainWindow window;
    window.resize(900, 900);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *panel = window.findChild<RulesPanel *>();
    ASSERT_TRUE(scene && panel);
    if (auto *side = window.findChild<QTabWidget *>(QStringLiteral("sidePanel")))
        side->setCurrentWidget(panel);
    settle();

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(shape, true);
    ASSERT_TRUE(scene->createBodyFromSelection() != nullptr);
    scene->clearPhysicsSelection();

    scene->setRules({ruleNamed("first"), ruleNamed("second"), ruleNamed("third")});
    scene->notifyRulesChanged();
    settle();
    ASSERT_EQ(orderOf(scene), (QStringList{"first", "second", "third"}));

    auto up = buttons(panel, "moveRuleUp");
    auto down = buttons(panel, "moveRuleDown");
    ASSERT_EQ(up.size(), 3);
    ASSERT_EQ(down.size(), 3);

    EXPECT_FALSE(up[0]->isEnabled()) << "the top rule has nowhere up to go";
    EXPECT_TRUE(down[0]->isEnabled());
    EXPECT_TRUE(up[2]->isEnabled());
    EXPECT_FALSE(down[2]->isEnabled()) << "and the last has nowhere down";

    down[0]->click();
    settle();
    EXPECT_EQ(orderOf(scene), (QStringList{"second", "first", "third"}))
        << "down swaps a rule with the one under it"
        << " -- " << orderOf(scene).join(", ").toStdString();

    up = buttons(panel, "moveRuleUp");
    up[2]->click();
    settle();
    EXPECT_EQ(orderOf(scene), (QStringList{"second", "third", "first"}))
        << "up swaps it with the one above"
        << " -- " << orderOf(scene).join(", ").toStdString();

    // Down the whole way and back, to be sure the ends hold.
    down = buttons(panel, "moveRuleDown");
    down[0]->click();
    settle();
    down = buttons(panel, "moveRuleDown");
    down[1]->click();
    settle();
    EXPECT_EQ(orderOf(scene), (QStringList{"third", "first", "second"}))
        << " -- " << orderOf(scene).join(", ").toStdString();

    EXPECT_TRUE(scene->rules().at(0).isValid()) << "and the rules are unharmed by moving";
}

// A dock is narrow. The cards are built to shrink into one, so nothing about
// them may set a floor that puts a scrollbar across the bottom -- that steals
// height from every card at once to show a strip nobody wants.
TEST(RuleWidth, Behaves)
{
    MainWindow window;
    window.resize(900, 900);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *panel = window.findChild<RulesPanel *>();
    ASSERT_TRUE(scene && panel);
    if (auto *side = window.findChild<QTabWidget *>(QStringLiteral("sidePanel")))
        side->setCurrentWidget(panel);
    settle();

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(shape, true);
    ASSERT_TRUE(scene->createBodyFromSelection() != nullptr);
    scene->clearPhysicsSelection();

    Rule named = ruleNamed("a rule with a considerably longer name than usual");
    scene->setRules({named, ruleNamed("second")});
    scene->notifyRulesChanged();
    settle();

    auto *scroll = panel->findChild<QScrollArea *>();
    ASSERT_TRUE(scroll != nullptr);
    EXPECT_EQ(scroll->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff)
        << "the panel never scrolls sideways";

    QFrame *card = nullptr;
    for (QFrame *f : panel->findChildren<QFrame *>())
        if (f->objectName() == QStringLiteral("ruleCard")) { card = f; break; }
    ASSERT_TRUE(card != nullptr);

    const int floorWidth = card->minimumSizeHint().width();
    std::printf("      card minimum width: %d px\n", floorWidth);
    EXPECT_LE(floorWidth, 245)
        << "a card fits a narrow dock without being clipped"
        << " -- needs " << floorWidth << " px";

    EXPECT_FALSE(scroll->horizontalScrollBar()->isVisible())
        << "and no bar is showing";
}

// A rule can be switched off without being taken away: the tick on its card
// says whether it runs, the file keeps it either way, and the simulation
// passes over it. Trying something without a rule in the way should not mean
// deleting the rule and writing it out again afterwards.
TEST(RuleEnabled, Behaves)
{
    MainWindow window;
    window.resize(900, 900);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *panel = window.findChild<RulesPanel *>();
    ASSERT_TRUE(scene && panel);
    if (auto *side = window.findChild<QTabWidget *>(QStringLiteral("sidePanel")))
        side->setCurrentWidget(panel);
    settle();

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(shape, true);
    ASSERT_TRUE(scene->createBodyFromSelection() != nullptr);
    scene->clearPhysicsSelection();

    scene->setRules({ruleNamed("first"), ruleNamed("second")});
    scene->notifyRulesChanged();
    settle();

    auto ticks = panel->findChildren<QCheckBox *>(QStringLiteral("ruleEnabled"));
    ASSERT_EQ(ticks.size(), 2) << "every card carries one";
    EXPECT_TRUE(ticks[0]->isChecked()) << "a new rule runs";
    EXPECT_TRUE(scene->rules().first().enabled);

    ticks[0]->setChecked(false);
    settle();
    EXPECT_FALSE(scene->rules().first().enabled) << "unticking switches it off";
    EXPECT_EQ(scene->rules().size(), 2) << "and keeps it";
    EXPECT_EQ(scene->rules().first().name, QStringLiteral("first")) << "unchanged";

    // Off is a state the file carries, not a display nicety.
    const QJsonObject saved = SceneSerializer::save(scene);
    const QJsonArray savedRules = saved.value(QStringLiteral("rules")).toArray();
    ASSERT_EQ(savedRules.size(), 2) << "a switched-off rule is still written out";
    EXPECT_FALSE(savedRules.at(0).toObject().value(QStringLiteral("enabled")).toBool(true))
        << "and written as off";

    ticks = panel->findChildren<QCheckBox *>(QStringLiteral("ruleEnabled"));
    ticks[0]->setChecked(true);
    settle();
    EXPECT_TRUE(scene->rules().first().enabled) << "and ticking brings it back";
}
