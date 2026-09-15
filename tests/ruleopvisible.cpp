#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "RulesPanel.h"

#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QAbstractSpinBox>
#include <QAbstractButton>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTabWidget>
#include <gtest/gtest.h>

// The card was once made narrow enough for a small dock by letting its combo
// boxes shrink without limit -- and the layout took it at its word: the choice
// of Set, Toggle, Negate or Add, and the choice of a typed value or another
// object's, were laid out zero pixels wide. Present, and impossible to use.

namespace {

void settle()
{
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();
}

QComboBox *comboOffering(RulesPanel *panel, const QString &entry)
{
    for (QComboBox *combo : panel->findChildren<QComboBox *>())
        if (combo->findText(entry) >= 0)
            return combo;
    return nullptr;
}

} // namespace

TEST(RuleOpVisible, TheOperationAndTheValueSourceCanBeSeen)
{
    MainWindow window;
    window.resize(1300, 900);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *panel = window.findChild<RulesPanel *>();
    ASSERT_TRUE(scene && panel);

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(shape, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    ASSERT_TRUE(body);
    scene->clearPhysicsSelection();

    Rule rule;
    rule.subjectName = body->name();
    rule.conditionKey = QStringLiteral("positionY");
    rule.compare = Rule::Compare::Greater;
    rule.conditionValue = 10.0;
    rule.targetName = body->name();
    rule.propertyKey = QStringLiteral("gravityScale");
    rule.op = Rule::Op::Set;
    rule.value = 1.0;
    scene->setRules({ rule });
    scene->notifyRulesChanged();

    for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->widget(i) == panel || tabs->widget(i)->isAncestorOf(panel))
                tabs->setCurrentIndex(i);
    settle();
    ASSERT_TRUE(panel->isVisible()) << "the rules are on screen";

    QComboBox *op = comboOffering(panel, QStringLiteral("Negate"));
    ASSERT_TRUE(op) << "the card offers the operations";
    EXPECT_TRUE(op->isVisible()) << "on screen";
    EXPECT_GE(op->width(), op->minimumSizeHint().width())
        << "and wide enough to read and pick from -- " << op->width() << " px";

    QComboBox *mode = comboOffering(panel, QStringLiteral("Property"));
    ASSERT_TRUE(mode) << "the card offers a value from another object";
    EXPECT_TRUE(mode->isVisible()) << "on screen, for a rule that sets a number";
    EXPECT_GE(mode->width(), mode->minimumSizeHint().width())
        << "and wide enough to read and pick from -- " << mode->width() << " px";

    op->setCurrentIndex(op->findText(QStringLiteral("Negate")));
    settle();
    EXPECT_EQ(scene->rules().first().op, Rule::Op::Negate) << "and Negate can be chosen";

    window.close();
}

// The rule's name is shown in its card's header, and double-clicking it is how
// the rule is renamed. Once, the header let the name shrink to nothing to fit a
// narrow dock -- and renaming was gone with it.
TEST(RuleOpVisible, TheNameIsShownAndDoubleClickRenames)
{
    MainWindow window;
    window.resize(1300, 900);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *panel = window.findChild<RulesPanel *>();
    ASSERT_TRUE(scene && panel);

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(shape, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    ASSERT_TRUE(body);
    scene->clearPhysicsSelection();

    Rule rule;
    rule.name = QStringLiteral("sink the ball");
    rule.subjectName = body->name();
    rule.conditionKey = QStringLiteral("positionY");
    rule.compare = Rule::Compare::Greater;
    rule.conditionValue = 10.0;
    rule.targetName = body->name();
    rule.propertyKey = QStringLiteral("gravityScale");
    rule.value = 1.0;
    scene->setRules({ rule });
    scene->notifyRulesChanged();

    for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->widget(i) == panel || tabs->widget(i)->isAncestorOf(panel))
                tabs->setCurrentIndex(i);
    settle();

    QLabel *heading = nullptr;
    for (QLabel *label : panel->findChildren<QLabel *>())
        if (label->property("ruleIndex").isValid())
            heading = label;
    ASSERT_TRUE(heading) << "the card has a heading";
    EXPECT_TRUE(heading->isVisible());
    EXPECT_GE(heading->width(), 40) << "wide enough to read and to double-click -- " << heading->width() << " px";

    const QPoint at(heading->width() / 2, heading->height() / 2);
    QMouseEvent twice(QEvent::MouseButtonDblClick, at, heading->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(heading, &twice);
    settle();

    bool editing = false;
    for (QLineEdit *edit : panel->findChildren<QLineEdit *>())
        editing = editing || (edit->property("ruleIndex").isValid() && edit->isVisible());
    EXPECT_TRUE(editing) << "double-clicking the name opens it for renaming";

    window.close();
}

// Twice now a control on the rule card was laid out zero pixels wide to make the
// card fit a narrow dock -- the operation choice, then the rule's name -- and
// each time it was found by someone using the app, not by a test. So: nothing
// on a card that is meant to be seen or used may be squeezed out of sight.
TEST(RuleOpVisible, NothingOnTheCardIsSqueezedAway)
{
    MainWindow window;
    window.resize(1300, 900);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *panel = window.findChild<RulesPanel *>();
    ASSERT_TRUE(scene && panel);

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(shape, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    ASSERT_TRUE(body);
    scene->clearPhysicsSelection();

    Rule setsValue;
    setsValue.name = QStringLiteral("sets a value");
    setsValue.subjectName = body->name();
    setsValue.conditionKey = QStringLiteral("positionY");
    setsValue.compare = Rule::Compare::Greater;
    setsValue.conditionValue = 10.0;
    setsValue.targetName = body->name();
    setsValue.propertyKey = QStringLiteral("gravityScale");
    setsValue.value = 1.0;
    scene->setRules({ setsValue });
    scene->notifyRulesChanged();

    for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->widget(i) == panel || tabs->widget(i)->isAncestorOf(panel))
                tabs->setCurrentIndex(i);
    settle();

    QFrame *card = nullptr;
    for (QFrame *frame : panel->findChildren<QFrame *>())
        if (frame->objectName() == QStringLiteral("ruleCard"))
            card = frame;
    ASSERT_TRUE(card);

    QStringList squeezed;
    for (QWidget *widget : card->findChildren<QWidget *>()) {
        if (!widget->isVisible())
            continue;
        const auto *label = qobject_cast<QLabel *>(widget);
        const bool control = qobject_cast<QComboBox *>(widget) || qobject_cast<QAbstractButton *>(widget)
                             || qobject_cast<QAbstractSpinBox *>(widget) || (label && !label->text().isEmpty());
        // Below its own smallest sensible size, and below a size anything can
        // be clicked at: a bare checkbox is 13 px wide and meant to be.
        if (control && widget->width() < qMin(widget->minimumSizeHint().width(), 16)) {
            squeezed << QStringLiteral("%1 '%2' %3px")
                            .arg(QString::fromLatin1(widget->metaObject()->className()),
                                 label ? label->text() : widget->objectName())
                            .arg(widget->width());
        }
    }
    EXPECT_TRUE(squeezed.isEmpty()) << "squeezed out of sight: " << squeezed.join(QStringLiteral(", ")).toStdString();

    window.close();
}
