#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RayItem.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "RulesPanel.h"
#include "ShapeItem.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QTabWidget>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();
}

// The Do row's mode picker: the one combo offering a typed value or a property.
QComboBox *modePicker(RulesPanel *rules)
{
    for (QComboBox *c : rules->findChildren<QComboBox *>())
        if (c->count() == 2 && c->itemText(1) == QStringLiteral("Property"))
            return c;
    return nullptr;
}

QStringList visibleLabels(RulesPanel *rules)
{
    QStringList out;
    for (QComboBox *c : rules->findChildren<QComboBox *>())
        if (c->isVisible())
            out << c->currentText();
    return out;
}

}

// A rule's number can be typed or read off another object, and the Do row
// switches between the two without ever showing both.
TEST(DoRow, ValueOrProperty)
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
    QComboBox *mode = modePicker(rules);
    ASSERT_NE(mode, nullptr) << "the Do row offers a mode";
    EXPECT_EQ(mode->currentText(), QStringLiteral("Value"))
        << "a rule with a literal starts in Value mode";

    int visibleSpins = 0;
    for (QDoubleSpinBox *s : rules->findChildren<QDoubleSpinBox *>())
        if (s->isVisible())
            ++visibleSpins;
    EXPECT_EQ(visibleSpins, 1) << "one editor for the typed number, no offset";

    QDir out(QDir::tempPath());
    QWidget *dock = rules;
    while (dock->parentWidget() && dock->height() < 300)
        dock = dock->parentWidget();
    dock->grab().save(out.filePath(QStringLiteral("dorow_value.png")));

    // Switching the picker is enough: the rule fills in an object and a
    // property by itself rather than sitting in a half-set state.
    mode->setCurrentIndex(mode->findData(true));
    settle();

    const Rule after = scene->rules().first();
    EXPECT_TRUE(after.usesSource()) << "picking Property completes the rule";
    EXPECT_FALSE(after.sourceObject.isEmpty());
    EXPECT_FALSE(after.sourceProperty.isEmpty());

    const QStringList labels = visibleLabels(rules);
    EXPECT_TRUE(labels.contains(QStringLiteral("Property")))
        << "the picker still says which mode this is";
    EXPECT_EQ(after.sourceObject, ray->name())
        << "it defaults to the object the rule already watches";
    bool objectShown = false;
    for (QComboBox *c : rules->findChildren<QComboBox *>())
        if (c->isVisible() && c->currentData().toString() == after.sourceObject)
            objectShown = true;
    EXPECT_TRUE(objectShown) << "the object it reads from is on screen";

    for (QDoubleSpinBox *s : rules->findChildren<QDoubleSpinBox *>()) {
        if (!s->isVisible())
            continue;
        EXPECT_DOUBLE_EQ(s->value(), after.sourceOffset)
            << "the typed value editor is gone, only the offset remains";
    }

    // What made the old layout unusable: a control squeezed to a sliver.
    for (QComboBox *c : rules->findChildren<QComboBox *>()) {
        if (!c->isVisible())
            continue;
        EXPECT_GT(c->width(), 60)
            << "readable: " << c->currentText().toStdString()
            << " at " << c->width() << " px";
    }

    dock->grab().save(out.filePath(QStringLiteral("dorow_property.png")));

    // ... and back, which must clear the source rather than leave it lying
    // about where a save would pick it up again.
    mode->setCurrentIndex(mode->findData(false));
    settle();
    EXPECT_FALSE(scene->rules().first().usesSource());
    EXPECT_TRUE(scene->rules().first().sourceObject.isEmpty());

    window.close();
}
