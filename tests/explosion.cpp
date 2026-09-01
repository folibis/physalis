#include "CanvasScene.h"
#include "MainWindow.h"
#include "ExplosionItem.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QAction>
#include <QApplication>
#include <gtest/gtest.h>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QLabel>
#include <QTableWidget>
#include <QTreeWidget>
#include <cstdio>

static void settle() { for (int i = 0; i < 20; ++i) QCoreApplication::processEvents(); }

TEST(Explosion, Behaves)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();
    auto *scene = window.findChild<CanvasScene *>();
    scene->setEditorMode(EditorMode::Physics);
    scene->setSnapToGrid(false);
    settle();

    auto *action = window.findChild<QAction *>(QStringLiteral("actionAddExplosion"));
    EXPECT_TRUE(action != nullptr) << "the toolbar offers Add Point";
    action->trigger();
    settle();
    EXPECT_TRUE(scene->explosions().size() == 1) << "a marker appears";
    ExplosionItem *explosion = scene->explosions().first();
    EXPECT_TRUE(explosion->name().startsWith(QStringLiteral("explosion"))) << "it is named" << " -- " << (explosion->name()).toStdString();
    EXPECT_TRUE(scene->shapes().isEmpty()) << "it is not a shape" << " -- " << (QStringLiteral("%1 shapes").arg(scene->shapes().size())).toStdString();

    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("canvasView"));
    const QPointF from = explosion->pos();
    const QPointF to = from + QPointF(120, 80);
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(from);
    press.setScreenPos(view->viewport()->mapToGlobal(view->mapFromScene(from)));
    press.setButton(Qt::LeftButton); press.setButtons(Qt::LeftButton);
    QApplication::sendEvent(scene, &press);
    QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
    move.setScenePos(to);
    move.setScreenPos(view->viewport()->mapToGlobal(view->mapFromScene(to)));
    move.setButtons(Qt::LeftButton);
    QApplication::sendEvent(scene, &move);
    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos(to);
    release.setScreenPos(view->viewport()->mapToGlobal(view->mapFromScene(to)));
    release.setButton(Qt::LeftButton);
    QApplication::sendEvent(scene, &release);
    settle();
    EXPECT_TRUE(explosion->pos() != from) << "it followed the drag" << " -- " << (QStringLiteral("(%1,%2)").arg(explosion->pos().x()).arg(explosion->pos().y())).toStdString();
    EXPECT_TRUE(scene->selectedExplosion() == explosion) << "and is selected";

    explosion->setPos(QPointF(0, 0));
    scene->world().gravity = QPointF(0.0, 0.0);
    QVector<ShapeItem *> debris;
    for (int i = 0; i < 4; ++i) {
        const qreal angle = i * M_PI / 2.0;
        auto *block = new RectangleItem;
        block->setRect(QRectF(0, 0, 30, 30));
        block->setPos(qCos(angle) * 100.0 - 15.0, qSin(angle) * 100.0 - 15.0);
        block->setName(QStringLiteral("debris%1").arg(i));
        scene->addItem(block);
        debris << block;
    }
    scene->notifyShapesChanged();
    QString firstBodyName;
    for (ShapeItem *s : debris) {
        scene->selectForPhysics(s, true);
        PhysicsBody *body = scene->createBodyFromSelection();
        if (firstBodyName.isEmpty())
            firstBodyName = body->name();   // readValue wants a body, not a shape
        scene->clearPhysicsSelection();
    }

    Rule bang;
    bang.subjectName = firstBodyName;
    bang.conditionKey = QStringLiteral("positionX");
    bang.compare = Rule::Compare::Greater;
    bang.conditionValue = -1e9;
    bang.targetName = explosion->name();          // the point, not a body
    bang.actionId = QStringLiteral("explode");
    explosion->setParam(QStringLiteral("impulse"), 300.0);
    explosion->setParam(QStringLiteral("radius"), 300.0);
    explosion->setParam(QStringLiteral("falloff"), 100.0);
    bang.once = true;
    scene->setRules({ bang });

    QVector<QPointF> before;
    for (ShapeItem *s : debris) before << s->sceneBoundingRect().center();

    auto *sim = window.findChild<SimulationController *>();
    sim->start();
    for (int f = 0; f < 60; ++f) sim->stepFrame();
    qreal moved = 0.0;
    for (int i = 0; i < debris.size(); ++i)
        moved += QLineF(before[i], debris[i]->sceneBoundingRect().center()).length();
    sim->stop();
    settle();
    EXPECT_TRUE(moved / debris.size() > 20.0) << "the blast happened at the marker";

    QString error;
    explosion->setPos(QPointF(123, 456));
    SceneSerializer::saveToFile(scene, QStringLiteral("explosion.phys"), &error);
    CanvasScene back;
    SceneSerializer::loadFromFile(&back, QStringLiteral("explosion.phys"), &error);
    EXPECT_TRUE(back.explosions().size() == 1
              && back.explosions().first()->pos() == QPointF(123, 456)) << "the point comes back" << " -- " << (back.explosions().isEmpty() ? QStringLiteral("none")
                                   : QStringLiteral("(%1,%2)")
                                         .arg(back.explosions().first()->pos().x())
                                         .arg(back.explosions().first()->pos().y())).toStdString();

    scene->selectExplosion(explosion);
    settle();
    {
        QStringList rows;
        for (QTableWidget *table : window.findChildren<QTableWidget *>())
            for (int r = 0; r < table->rowCount(); ++r) {
                QWidget *cell = table->cellWidget(r, 0);
                auto *text = cell ? cell->findChild<QLabel *>() : nullptr;
                if (text) rows << text->text();
            }
        EXPECT_TRUE(rows.contains(QStringLiteral("Name"))) << "Name is shown";
        EXPECT_TRUE(rows.contains(QStringLiteral("X"))
                  && rows.contains(QStringLiteral("Y"))) << "X and Y are shown";
        EXPECT_TRUE(rows.contains(QStringLiteral("Impulse"))
                  && rows.contains(QStringLiteral("Radius"))
                  && rows.contains(QStringLiteral("Falloff"))) << "and the blast settings are here, not on the rule";
    }

    {
        bool found = false;
        for (QTreeWidget *tree : window.findChildren<QTreeWidget *>()) {
            QTreeWidgetItemIterator it(tree);
            for (; *it; ++it)
                if ((*it)->text(0) == explosion->name())
                    found = true;
        }
        EXPECT_TRUE(found) << "listed by name" << " -- " << (explosion->name()).toStdString();
    }

    window.close();
}
