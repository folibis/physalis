// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "EditorMode.h"
#include "RulesPanel.h"
#include "Rule.h"
#include "SceneVariable.h"

#include <QComboBox>
#include <QToolButton>
#include <QCoreApplication>
#include <gtest/gtest.h>

namespace {

// The action row's "what to do" box, found by what it offers rather than by
// reaching into the panel: it is the one combo box carrying "Set to".
QComboBox *opBox(RulesPanel *panel)
{
    for (QComboBox *combo : panel->findChildren<QComboBox *>()) {
        if (combo->findText(QStringLiteral("Set to")) >= 0)
            return combo;
    }
    return nullptr;
}

// The box naming the property an action writes: the one offering our two
// variables by name.
QComboBox *propertyBox(RulesPanel *panel)
{
    for (QComboBox *combo : panel->findChildren<QComboBox *>()) {
        if (combo->findText(QStringLiteral("clock")) >= 0
            && combo->findText(QStringLiteral("score")) >= 0) {
            return combo;
        }
    }
    return nullptr;
}

bool offers(QComboBox *combo, const char *text)
{
    return combo && combo->findText(QString::fromLatin1(text)) >= 0;
}

} // namespace

// The list of things an action can do belongs to the property it writes: a timer
// is started and wound back, a score is toggled and negated, and neither list
// makes sense on the other. The box is rebuilt when the property changes, which
// is the part worth a test -- it happens from inside that property box's own
// signal, and rebuilding a widget from there has taken this application down
// more than once. It is queued, so the event loop has to be let run.
TEST(TimerVariable, TheOpBoxFollowsWhetherThePropertyIsATimer)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);

    SceneVariable score;
    score.name = QStringLiteral("score");
    score.type = SceneVariable::Type::Integer;
    score.initial = 0;

    SceneVariable clock;
    clock.name = QStringLiteral("clock");
    clock.type = SceneVariable::Type::Timer;
    clock.initial = 0;
    scene.setVariables({ score, clock });

    Rule rule;
    rule.conditions[0].subjectName = Rule::world();
    rule.conditions[0].eventId = Rule::runStartedEvent();
    rule.actions[0].targetName = Rule::variables();
    rule.actions[0].propertyKey = QStringLiteral("score");
    rule.actions[0].op = Rule::Op::Set;
    scene.setRules({ rule });

    RulesPanel panel(&scene, nullptr);

    QComboBox *op = opBox(&panel);
    ASSERT_NE(op, nullptr) << "the action row has no op box";
    EXPECT_TRUE(offers(op, "Toggle")) << "an ordinary variable lost Toggle";
    EXPECT_FALSE(offers(op, "Start")) << "a plain number was offered a timer's verbs";

    // Point the action at the timer instead. The rebuild is queued behind this.
    QComboBox *property = propertyBox(&panel);
    ASSERT_NE(property, nullptr) << "the action row has no property box";
    property->setCurrentIndex(property->findText(QStringLiteral("clock")));
    QCoreApplication::processEvents();

    op = opBox(&panel);
    ASSERT_NE(op, nullptr) << "the op box did not survive the property changing";
    EXPECT_TRUE(offers(op, "Start")) << "a timer was not offered Start";
    EXPECT_TRUE(offers(op, "Pause")) << "a timer was not offered Pause";
    EXPECT_TRUE(offers(op, "Stop")) << "a timer was not offered Stop";
    EXPECT_TRUE(offers(op, "Reset")) << "a timer was not offered Reset";
    EXPECT_FALSE(offers(op, "Toggle")) << "a timer was offered Toggle, which it has no use for";
    EXPECT_FALSE(offers(op, "Negate")) << "a timer was offered Negate, which it has no use for";

    // And an op the new list cannot show must not be left on the rule: picked
    // Start, then moved back to the score, the rule would hold a verb its own
    // card no longer offers.
    op->setCurrentIndex(op->findText(QStringLiteral("Start")));
    QCoreApplication::processEvents();
    ASSERT_FALSE(scene.rules().isEmpty());
    EXPECT_EQ(scene.rules().at(0).actions[0].op, Rule::Op::TimerStart);

    property->setCurrentIndex(property->findText(QStringLiteral("score")));
    QCoreApplication::processEvents();
    EXPECT_FALSE(Rule::isTimerVerb(scene.rules().at(0).actions[0].op))
        << "a plain number was left holding a timer verb";
}

// The two buttons beside the title fold every card at once. Each is greyed out
// when it has nothing left to do, which is also what says whether the panel
// believes the cards are folded.
TEST(RulesPanel, CollapseAllAndExpandAllFoldEveryCard)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);

    QVector<Rule> rules;
    for (int i = 0; i < 3; ++i) {
        Rule rule;
        rule.name = QStringLiteral("rule%1").arg(i);
        rule.conditions[0].subjectName = Rule::world();
        rule.conditions[0].eventId = Rule::runStartedEvent();
        rule.actions[0].targetName = Rule::world();
        rule.actions[0].actionId = Rule::stopRunAction();
        rules.append(rule);
    }
    scene.setRules(rules);

    RulesPanel panel(&scene, nullptr);

    QToolButton *collapseAll = nullptr;
    QToolButton *expandAll = nullptr;
    for (QToolButton *button : panel.findChildren<QToolButton *>()) {
        if (button->toolTip() == QStringLiteral("Collapse all rules"))
            collapseAll = button;
        else if (button->toolTip() == QStringLiteral("Expand all rules"))
            expandAll = button;
    }
    ASSERT_NE(collapseAll, nullptr) << "no collapse-all button beside the title";
    ASSERT_NE(expandAll, nullptr) << "no expand-all button beside the title";

    // Cards start open, so there is nothing to expand and everything to fold.
    EXPECT_TRUE(collapseAll->isEnabled());
    EXPECT_FALSE(expandAll->isEnabled()) << "expand all was offered with nothing folded";

    collapseAll->click();
    EXPECT_FALSE(collapseAll->isEnabled()) << "collapse all was still offered with all folded";
    EXPECT_TRUE(expandAll->isEnabled());

    expandAll->click();
    EXPECT_TRUE(collapseAll->isEnabled());
    EXPECT_FALSE(expandAll->isEnabled()) << "expand all was still offered with none folded";
}

// A rule is marked incomplete when it names a property the engine does not
// offer -- which is what catches a scene drawn for an older plugin. But two of
// the world's readings are the application's own and appear in no catalogue, so
// counting frames looked like naming something that had been dropped, and every
// scene doing it opened with a red card over a rule that was perfectly fine.
TEST(RulesPanel, TheWorldsOwnReadingsAreNotMistakenForDroppedOnes)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    const auto everyNthFrame = [](const QString &key) {
        Rule rule;
        rule.conditions[0].subjectName = Rule::world();
        rule.conditions[0].conditionKey = key;
        rule.conditions[0].compare = Rule::Compare::Multiple;
        rule.conditions[0].conditionValue = 200;
        rule.actions[0].targetName = Rule::world();
        rule.actions[0].actionId = Rule::stopRunAction();
        return rule;
    };

    scene.setRules({ everyNthFrame(QStringLiteral("frame")),
                     everyNthFrame(QStringLiteral("time")) });
    RulesPanel panel(&scene, nullptr);
    EXPECT_EQ(panel.incompleteCount(), 0)
        << "a rule watching the run's own frame count or elapsed time was marked"
           " incomplete, though both are offered by the condition's property list";

    // And the check still does its job: a property no catalogue has is caught.
    scene.setRules({ everyNthFrame(QStringLiteral("framesPerFortnight")) });
    RulesPanel strict(&scene, nullptr);
    EXPECT_EQ(strict.incompleteCount(), 1)
        << "a rule naming a property the engine never offered was not marked";
}

// The mark says a card does not run; the card has to say why. It used to be on
// a 14-pixel icon beside the name and nowhere else, which is a small thing to
// have to find on a card that is already red.
TEST(RulesPanel, AMarkedCardCarriesItsReasonAsATooltip)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    Rule rule;                       // watched nothing: NoSubject
    rule.actions[0].targetName = Rule::world();
    rule.actions[0].actionId = Rule::stopRunAction();
    scene.setRules({ rule });

    RulesPanel panel(&scene, nullptr);
    ASSERT_EQ(panel.incompleteCount(), 1) << "the rule under test is not actually marked";

    // Every reason starts the same way, so the panel can be asked whether it
    // says one without reaching for the private text that produces it.
    int carriers = 0;
    QWidget *widest = nullptr;
    for (QWidget *widget : panel.findChildren<QWidget *>()) {
        if (!widget->toolTip().contains(QStringLiteral("does not run")))
            continue;
        ++carriers;
        if (!widest || widget->findChildren<QWidget *>().size()
                           > widest->findChildren<QWidget *>().size()) {
            widest = widget;
        }
    }
    EXPECT_GE(carriers, 2)
        << "only one thing on the card says why it is marked -- the small icon beside"
           " the name -- so hovering the card itself explains nothing";
    ASSERT_NE(widest, nullptr);
    EXPECT_FALSE(widest->findChildren<QWidget *>().isEmpty())
        << "the reason is on a leaf widget only, not on the card that holds the row";
}

// A condition the engine no longer offers has to survive being looked at. The
// watch box is built from the engine's catalogue, so a scene drawn for an older
// plugin names things that are not in it -- and the box used to take whatever
// sat on top and write it back, which replaced the rule with a different one,
// permanently, just for opening the panel. The action row was fixed for this
// long ago; the condition row never was.
TEST(RulesPanel, OpeningThePanelDoesNotRewriteAConditionItCannotShow)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    Rule rule;
    rule.conditions[0].subjectName = Rule::world();
    rule.conditions[0].conditionKey = QStringLiteral("framesPerFortnight");
    rule.conditions[0].compare = Rule::Compare::Multiple;
    rule.conditions[0].conditionValue = 200;
    rule.actions[0].targetName = Rule::world();
    rule.actions[0].actionId = Rule::stopRunAction();
    scene.setRules({ rule });

    RulesPanel panel(&scene, nullptr);

    const RuleCondition &kept = scene.rules().at(0).conditions.at(0);
    EXPECT_EQ(kept.conditionKey, QStringLiteral("framesPerFortnight"))
        << "the panel replaced a condition it could not display instead of keeping it";
    EXPECT_TRUE(kept.eventId.isEmpty())
        << "the panel turned a value comparison into an event";
    EXPECT_EQ(kept.conditionValue.toInt(), 200) << "the value it compared against was lost";
    EXPECT_EQ(panel.incompleteCount(), 1)
        << "the rule names a property this engine does not offer and was not marked";
}

// The run's own two readings are offered on the world's watch list, and no
// engine publishes them -- which is why the check that marks a rule for naming
// a property the engine dropped had to be taught about them separately. If that
// ever turns into taking them off the list, this says so.
TEST(RulesPanel, TheWorldOffersItsFrameCountAndElapsedTime)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    Rule rule;
    rule.conditions[0].subjectName = Rule::world();
    rule.conditions[0].conditionKey = QStringLiteral("frame");
    rule.conditions[0].compare = Rule::Compare::Multiple;
    rule.conditions[0].conditionValue = 200;
    rule.actions[0].targetName = Rule::world();
    rule.actions[0].actionId = Rule::stopRunAction();
    scene.setRules({ rule });

    RulesPanel panel(&scene, nullptr);

    QComboBox *watch = nullptr;
    for (QComboBox *combo : panel.findChildren<QComboBox *>()) {
        if (combo->findData(QStringLiteral("frame")) >= 0)
            watch = combo;
    }
    ASSERT_NE(watch, nullptr) << "the world's watch list no longer offers the frame count";
    EXPECT_GE(watch->findData(QStringLiteral("time")), 0)
        << "the world's watch list no longer offers the elapsed time";
    EXPECT_EQ(watch->currentData().toString(), QStringLiteral("frame"))
        << "the rule watches the frame count, but the box is showing something else";
}
