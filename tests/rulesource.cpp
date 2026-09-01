#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RayItem.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "RulesPanel.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QTest>
#include <QToolButton>
#include <QTabWidget>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();
}

QComboBox *comboWithItem(RulesPanel *rules, const QString &text)
{
    for (QComboBox *c : rules->findChildren<QComboBox *>())
        for (int i = 0; i < c->count(); ++i)
            if (c->itemText(i) == text)
                return c;
    return nullptr;
}

}

// Changing where a rule reads its value from rebuilds the very combo boxes the
// change came out of. Doing that while their signal is still on the stack is
// what crashed the application.
TEST(RuleSource, SurvivesChangingTheSource)
{
    MainWindow window;
    window.resize(1300, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    scene->setEditorMode(EditorMode::Physics);

    auto *block = new RectangleItem;
    block->setRect(QRectF(0, 0, 80, 40));
    block->setName(QStringLiteral("block"));
    scene->addItem(block);
    scene->notifyShapesChanged();
    scene->selectForPhysics(block, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    RayItem *ray = scene->addRay(QPointF(-200, 0));

    Rule r;
    r.subjectName = ray->name();
    r.eventId = QStringLiteral("rayDetects");
    r.targetName = body->name();
    r.propertyKey = QStringLiteral("positionX");
    r.op = Rule::Op::Set;
    r.value = 0.0;
    scene->setRules({ r });

    for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i).contains(QStringLiteral("Rule")))
                tabs->setCurrentIndex(i);
    settle();

    auto *rules = window.findChild<RulesPanel *>();
    QComboBox *mode = comboWithItem(rules, QStringLiteral("Property"));
    ASSERT_NE(mode, nullptr);

    mode->setCurrentIndex(mode->findData(true));
    settle();
    ASSERT_TRUE(scene->rules().first().usesSource());

    // The object it reads from: switching it rebuilds the property list, and
    // the combo that emitted the change goes with it.
    QComboBox *object = nullptr;
    for (QComboBox *c : rules->findChildren<QComboBox *>())
        if (c->isVisible() && c->currentData().toString() == scene->rules().first().sourceObject)
            object = c;
    ASSERT_NE(object, nullptr);
    ASSERT_GT(object->count(), 1) << "there is something else to switch to";

    const int other = object->currentIndex() == 0 ? 1 : 0;
    const QString wanted = object->itemData(other).toString();
    object->setCurrentIndex(other);
    settle();
    EXPECT_EQ(scene->rules().first().sourceObject, wanted) << "the rule followed";
    EXPECT_FALSE(scene->rules().first().sourceProperty.isEmpty())
        << "and picked a property that object actually has";

    // Back to a typed value, which tears the source controls down again.
    QComboBox *modeAgain = comboWithItem(rules, QStringLiteral("Property"));
    ASSERT_NE(modeAgain, nullptr);
    modeAgain->setCurrentIndex(modeAgain->findData(false));
    settle();
    EXPECT_FALSE(scene->rules().first().usesSource());

    window.close();
}

// Double-clicking a rule's caption renames it, and the name is what the card
// shows from then on.
TEST(RuleSource, RenamesOnDoubleClick)
{
    MainWindow window;
    window.resize(1300, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    scene->setEditorMode(EditorMode::Physics);

    auto *block = new RectangleItem;
    block->setRect(QRectF(0, 0, 80, 40));
    block->setName(QStringLiteral("block"));
    scene->addItem(block);
    scene->notifyShapesChanged();
    scene->selectForPhysics(block, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();

    Rule r;
    r.subjectName = body->name();
    r.eventId = QStringLiteral("contactBegin");
    r.targetName = body->name();
    r.propertyKey = QStringLiteral("positionX");
    r.op = Rule::Op::Set;
    r.value = 0.0;
    scene->setRules({ r });

    for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i).contains(QStringLiteral("Rule")))
                tabs->setCurrentIndex(i);
    settle();

    auto *rules = window.findChild<RulesPanel *>();
    QLabel *caption = nullptr;
    for (QLabel *l : rules->findChildren<QLabel *>())
        if (l->text() == QStringLiteral("Rule 1"))
            caption = l;
    ASSERT_NE(caption, nullptr) << "the card is numbered until it is named";

    QTest::mouseDClick(caption, Qt::LeftButton);
    settle();

    QLineEdit *edit = nullptr;
    for (QLineEdit *e : rules->findChildren<QLineEdit *>())
        if (e->isVisible() && e->property("ruleIndex").isValid())
            edit = e;
    ASSERT_NE(edit, nullptr) << "a text box takes the caption's place";
    EXPECT_FALSE(caption->isVisible());
    EXPECT_TRUE(edit->text().isEmpty()) << "the number is a placeholder, not a name";

    edit->setText(QStringLiteral("reverse at the wall"));
    QTest::keyClick(edit, Qt::Key_Return);
    settle();

    EXPECT_EQ(scene->rules().first().name, QStringLiteral("reverse at the wall"));
    EXPECT_EQ(caption->text(), QStringLiteral("reverse at the wall"));
    EXPECT_TRUE(caption->isVisible());
    EXPECT_FALSE(edit->isVisible());

    // Escape leaves the name alone.
    QTest::mouseDClick(caption, Qt::LeftButton);
    settle();
    edit->setText(QStringLiteral("something else"));
    QTest::keyClick(edit, Qt::Key_Escape);
    settle();
    EXPECT_EQ(scene->rules().first().name, QStringLiteral("reverse at the wall"));

    // Clearing it puts the numbering back.
    QTest::mouseDClick(caption, Qt::LeftButton);
    settle();
    edit->clear();
    QTest::keyClick(edit, Qt::Key_Return);
    settle();
    EXPECT_TRUE(scene->rules().first().name.isEmpty());
    EXPECT_EQ(caption->text(), QStringLiteral("Rule 1"));

    window.close();
}

// The delete button lives inside the card the deletion tears down, which is
// the same hazard from the other end.
TEST(RuleSource, SurvivesRemovingItsOwnRule)
{
    MainWindow window;
    window.resize(1300, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    scene->setEditorMode(EditorMode::Physics);

    auto *block = new RectangleItem;
    block->setRect(QRectF(0, 0, 80, 40));
    block->setName(QStringLiteral("block"));
    scene->addItem(block);
    scene->notifyShapesChanged();
    scene->selectForPhysics(block, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();

    Rule r;
    r.subjectName = body->name();
    r.eventId = QStringLiteral("contactBegin");
    r.targetName = body->name();
    r.propertyKey = QStringLiteral("positionX");
    r.op = Rule::Op::Set;
    r.value = 0.0;
    scene->setRules({ r, r });

    for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i).contains(QStringLiteral("Rule")))
                tabs->setCurrentIndex(i);
    settle();

    auto *rules = window.findChild<RulesPanel *>();
    QToolButton *remove = nullptr;
    for (QToolButton *b : rules->findChildren<QToolButton *>())
        if (b->toolTip().contains(QStringLiteral("Remove")))
            remove = b;
    ASSERT_NE(remove, nullptr);

    QTest::mouseClick(remove, Qt::LeftButton);
    settle();
    EXPECT_EQ(scene->rules().size(), 1) << "the rule went, and the app is still here";

    window.close();
}
