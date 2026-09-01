#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "ShapeItem.h"
#include "RectangleItem.h"
#include "CircleItem.h"
#include "Joint.h"
#include "RulesPanel.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QComboBox>
#include <QAbstractItemView>
#include <QPixmap>
#include <QVariantMap>
#include <cstdio>

static const char *kScratch = "";


TEST(RuleIcons, Behaves)
{
    CanvasScene scene;

    auto *r = new RectangleItem;
    r->setRect(QRectF(0, 0, 60, 40));
    r->setPos(100, 100);
    scene.addItem(r);
    auto *c = new CircleItem;
    c->setRect(QRectF(0, 0, 50, 50));
    c->setPos(260, 100);
    scene.addItem(c);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    scene.selectForPhysics(r, true);
    PhysicsBody *bodyA = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    scene.selectForPhysics(c, true);
    PhysicsBody *bodyB = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    scene.createJoint(QStringLiteral("revolute"), bodyA, bodyB, 1, QVariantMap());

    RulesPanel panel(&scene, nullptr);
    panel.resize(430, 340);
    panel.show();
    QCoreApplication::processEvents();

    // Add one rule, so a card exists with its combos on it.
    for (QWidget *w : panel.findChildren<QWidget *>())
        if (w->inherits("QToolButton") && w->toolTip().contains(QLatin1String("rule"),
                                                                Qt::CaseInsensitive))
            QMetaObject::invokeMethod(w, "click");
    QCoreApplication::processEvents();

    int withIcons = 0, withSuffix = 0, objectEntries = 0;
    for (QComboBox *combo : panel.findChildren<QComboBox *>()) {
        combo->showPopup();   // the combos fill on demand
        combo->hidePopup();
        for (int i = 0; i < combo->count(); ++i) {
            const QString text = combo->itemText(i);
            const QString data = combo->itemData(i).toString();
            if (data.isEmpty() || data.startsWith(QLatin1Char('@')))
                continue;   // "anything", "the shape that touched it", properties
            if (data != text)
                continue;   // property entries: label differs from the key
            ++objectEntries;
            if (!combo->itemIcon(i).isNull())
                ++withIcons;
            if (text.contains(QLatin1String("(body)")) || text.contains(QLatin1String("(shape)"))
                || text.contains(QLatin1String("(joint)")))
                ++withSuffix;
        }
    }
    EXPECT_TRUE(objectEntries > 0 && withIcons == objectEntries) << "every object entry carries an icon";
    EXPECT_TRUE(withSuffix == 0) << "none of them still says (body)/(shape)/(joint)";

    panel.grab().save(QString::fromLatin1(kScratch) + QStringLiteral("rulecard.png"));

    // The open list, which is where the icons do their work.
    if (QComboBox *first = panel.findChildren<QComboBox *>().value(0)) {
        first->showPopup();
        for (int i = 0; i < 20; ++i)
            QCoreApplication::processEvents();
        if (QWidget *popup = first->view()->window())
            popup->grab().save(QString::fromLatin1(kScratch) + QStringLiteral("ruledropdown.png"));
        first->hidePopup();
    }
}
