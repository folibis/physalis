#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "PropertyPanel.h"
#include "RectangleItem.h"
#include "Rule.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QTabWidget>
#include <gtest/gtest.h>

// Renaming the way it is done in the window -- typing into the Name row of the
// property table -- and then looking at everything that named the object: the
// log over the canvas, and the object lists on the rule cards.

namespace {

void settle()
{
    for (int i = 0; i < 40; ++i)
        QCoreApplication::processEvents();
}

// The Name row's editor: the one in the property table holding the name. A
// spin box has a line edit of its own, so the text is what tells them apart.
QLineEdit *editorHolding(MainWindow &window, const QString &text)
{
    auto *panel = window.findChild<PropertyPanel *>();
    if (!panel)
        return nullptr;
    for (QLineEdit *edit : panel->findChildren<QLineEdit *>()) {
        if (edit->text() == text)
            return edit;
    }
    return nullptr;
}

QString logText(MainWindow &window)
{
    auto *overlay = window.findChild<QLabel *>(QStringLiteral("LogOverlay"));
    return overlay && overlay->isVisible() ? overlay->text() : QString();
}

bool someComboOffers(MainWindow &window, const QString &name)
{
    for (QComboBox *combo : window.findChildren<QComboBox *>()) {
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemText(i) == name)
                return true;
        }
    }
    return false;
}

} // namespace

TEST(RenameInApp, TheLogAndTheRuleCardsFollow)
{
    MainWindow window;
    window.resize(1300, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    ShapeItem *box = scene->addRectangle(QPointF(0, 0));
    box->setName(QStringLiteral("box"));
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(box, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    ASSERT_TRUE(body);
    body->props().type = physics::BodyType::Dynamic;

    // A rule that names the shape, and the shape's mass in the log.
    Rule rule;
    rule.subjectName = QStringLiteral("box");
    rule.eventId = QStringLiteral("contactBegin");
    rule.targetName = body->name();
    rule.propertyKey = QStringLiteral("velocityX");
    rule.op = Rule::Op::Set;
    rule.value = 10.0;
    scene->setRules({ rule });
    CanvasScene::Watch watch;
    watch.objectName = QStringLiteral("box");
    watch.propertyKey = QStringLiteral("mass");
    watch.label = QStringLiteral("Mass (kg)");
    scene->addWatch(watch);
    settle();

    // Renamed the way the window does it: typed into the Name row.
    QLineEdit *name = editorHolding(window, QStringLiteral("box"));
    ASSERT_TRUE(name) << "no Name editor in the property table";
    name->setText(QStringLiteral("crate"));
    name->editingFinished();
    settle();
    ASSERT_EQ(box->name(), QStringLiteral("crate")) << "typing the name did not rename the shape";

    EXPECT_EQ(scene->watches().first().objectName, QStringLiteral("crate"));
    EXPECT_EQ(scene->rules().first().subjectName, QStringLiteral("crate"));

    // What a rule card offers as an object, once it has been rebuilt.
    EXPECT_TRUE(someComboOffers(window, QStringLiteral("crate")))
        << "the rule cards still offer the old name";
    EXPECT_FALSE(someComboOffers(window, QStringLiteral("box")))
        << "the rule cards still offer the old name";

    // And the log over the canvas, which is only there during a run.
    window.findChild<QAction *>(QStringLiteral("actionSimulate"))->trigger();
    settle();
    const QString shown = logText(window);
    EXPECT_TRUE(shown.contains(QStringLiteral("crate"))) << shown.toStdString();
    EXPECT_FALSE(shown.contains(QStringLiteral("box"))) << shown.toStdString();
    EXPECT_TRUE(shown.contains(QStringLiteral("Mass"))) << shown.toStdString();
    window.findChild<QAction *>(QStringLiteral("actionStop"))->trigger();
    settle();
}

// Enter on a text field: the value the object took comes back into the box, so
// a name that had to be made unique is not left showing the one that was typed.
TEST(RenameInApp, TheFieldShowsWhatWasAccepted)
{
    MainWindow window;
    window.resize(1300, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    ShapeItem *first = scene->addRectangle(QPointF(0, 0));
    first->setName(QStringLiteral("gate"));
    ShapeItem *second = scene->addRectangle(QPointF(200, 0));
    second->setName(QStringLiteral("box"));
    scene->notifyShapesChanged();
    scene->selectShape(second);
    settle();

    QLineEdit *name = editorHolding(window, QStringLiteral("box"));
    ASSERT_TRUE(name) << "no Name editor in the property table";
    name->setFocus();
    name->setText(QStringLiteral("gate"));
    name->editingFinished();
    settle();

    EXPECT_NE(second->name(), QStringLiteral("gate")) << "two shapes cannot share a name";
    EXPECT_EQ(name->text(), second->name()) << "the field kept a name the shape does not have";
    // Selected, so the edit visibly landed -- where the field has the focus at
    // all. Run without a window manager to activate the window, nothing does.
    if (name->hasFocus())
        EXPECT_EQ(name->selectedText(), second->name()) << "nothing showed that Enter was taken";
    EXPECT_EQ(first->name(), QStringLiteral("gate"));
}
