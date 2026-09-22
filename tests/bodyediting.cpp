// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "CircleItem.h"
#include "PhysicsBody.h"
#include "PropertyPane/PhysicsPropertyPane.h"
#include "RectangleItem.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"
#include "UndoStack.h"

#include <QJsonObject>
#include <gtest/gtest.h>

// Building a body out of shapes, step by step, with every step undone and
// redone and the file written and read back at the end. The suite has a test
// per remembered incident -- a double-click that made the wrong kind of body,
// a rename that lost a rule -- and nothing that walks the whole of what
// editing a body is. This does: make, add, retype, detach, delete.

namespace {

ShapeItem *rectangle(CanvasScene *scene, const QString &name, const QPointF &at)
{
    ShapeItem *shape = scene->addRectangle(at);
    shape->setRect(QRectF(0, 0, 60, 40));
    shape->setName(name);
    return shape;
}

PhysicsBody *bodyNamed(const CanvasScene &scene, const QString &name)
{
    for (PhysicsBody *body : scene.bodies()) {
        if (body->name() == name)
            return body;
    }
    return nullptr;
}

int shapeCountOf(const CanvasScene &scene, const QString &bodyName)
{
    PhysicsBody *body = bodyNamed(scene, bodyName);
    return body ? body->shapes().size() : -1;
}

} // namespace

// Each edit is a state on the undo stack, and undoing walks back through every
// one of them to the empty scene, redoing forward to where it left off.
TEST(BodyEditing, EveryStepUndoesAndRedoes)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    UndoStack undo(&scene);
    scene.setEditorMode(EditorMode::Physics);

    ShapeItem *first = rectangle(&scene, QStringLiteral("hull"), QPointF(0, 0));
    scene.notifyShapesChanged();
    undo.push(QStringLiteral("Add hull"));

    scene.selectForPhysics(first);
    PhysicsBody *body = scene.createBodyFromSelection();
    ASSERT_TRUE(body);
    body->setName(QStringLiteral("cart"));
    scene.clearPhysicsSelection();
    undo.push(QStringLiteral("Make cart"));
    ASSERT_EQ(shapeCountOf(scene, QStringLiteral("cart")), 1);

    // A second shape into the same body.
    ShapeItem *second = rectangle(&scene, QStringLiteral("roof"), QPointF(0, -60));
    scene.notifyShapesChanged();
    bodyNamed(scene, QStringLiteral("cart"))->addShape(second);
    undo.push(QStringLiteral("Add roof to cart"));
    ASSERT_EQ(shapeCountOf(scene, QStringLiteral("cart")), 2);

    // A different kind of body.
    bodyNamed(scene, QStringLiteral("cart"))->props().type = physics::BodyType::Static;
    undo.push(QStringLiteral("Make cart static"));

    // And one shape back out of it.
    PhysicsBody *cart = bodyNamed(scene, QStringLiteral("cart"));
    cart->removeShape(cart->shapes().last());
    undo.push(QStringLiteral("Take the roof off"));
    ASSERT_EQ(shapeCountOf(scene, QStringLiteral("cart")), 1);

    // Back, one step at a time.
    undo.undo();
    EXPECT_EQ(shapeCountOf(scene, QStringLiteral("cart")), 2) << "the roof is back on";
    EXPECT_EQ(bodyNamed(scene, QStringLiteral("cart"))->props().type, physics::BodyType::Static);

    undo.undo();
    EXPECT_EQ(bodyNamed(scene, QStringLiteral("cart"))->props().type, physics::BodyType::Dynamic)
        << "undoing the retype leaves the body it was made as";

    undo.undo();
    EXPECT_EQ(shapeCountOf(scene, QStringLiteral("cart")), 1) << "the roof is off again";

    undo.undo();
    EXPECT_EQ(scene.bodies().size(), 0) << "undoing the body leaves the shape loose";
    EXPECT_EQ(scene.shapes().size(), 1);

    // And forward again to where it stood.
    while (undo.canRedo())
        undo.redo();
    EXPECT_EQ(shapeCountOf(scene, QStringLiteral("cart")), 1);
    EXPECT_EQ(bodyNamed(scene, QStringLiteral("cart"))->props().type, physics::BodyType::Static);
    EXPECT_EQ(scene.shapes().size(), 2) << "the shape taken out of the body is still on the canvas";
}

// Whatever a body is made of and made as, a save and a load give back the
// same thing -- and the engine builds it.
TEST(BodyEditing, WhatWasBuiltSurvivesTheFileAndReachesTheEngine)
{
    for (physics::BodyType type : { physics::BodyType::Dynamic, physics::BodyType::Static,
                                    physics::BodyType::Kinematic }) {
        CanvasScene scene;
        scene.setSimulationEngineName(QStringLiteral("Box2D"));
        scene.setEditorMode(EditorMode::Physics);

        ShapeItem *hull = rectangle(&scene, QStringLiteral("hull"), QPointF(0, 0));
        auto *wheel = new CircleItem;
        wheel->setRect(QRectF(0, 0, 40, 40));
        wheel->setPos(80, 0);
        wheel->setName(QStringLiteral("wheel"));
        scene.addItem(wheel);
        scene.notifyShapesChanged();

        scene.selectForPhysics(hull);
        scene.selectForPhysics(wheel, true);
        PhysicsBody *body = scene.createBodyFromSelection();
        ASSERT_TRUE(body) << "two shapes make one body";
        body->setName(QStringLiteral("cart"));
        body->props().type = type;
        scene.clearPhysicsSelection();

        const QJsonObject document = SceneSerializer::save(&scene);
        CanvasScene reopened;
        QString error;
        ASSERT_TRUE(SceneSerializer::load(&reopened, document, &error)) << error.toStdString();

        PhysicsBody *loaded = bodyNamed(reopened, QStringLiteral("cart"));
        ASSERT_TRUE(loaded) << "the body came back by name";
        EXPECT_EQ(loaded->props().type, type) << "and as the kind it was made";
        EXPECT_EQ(loaded->shapes().size(), 2) << "with both its shapes";

        // The engine is handed it: one body, its two parts.
        const physics::BodyDesc desc = loaded->toBodyDesc();
        EXPECT_EQ(desc.type, type);
        EXPECT_EQ(desc.parts.size(), 2);

        SimulationController sim(&reopened, nullptr);
        sim.setEngineName(QStringLiteral("Box2D"));
        sim.start();
        EXPECT_TRUE(sim.skippedBodies().isEmpty())
            << "the engine took the body: " << sim.skippedBodies().join(QLatin1Char(',')).toStdString();
        EXPECT_TRUE(sim.readValue(QStringLiteral("hull"), QStringLiteral("friction")).isValid())
            << "and both its shapes answer by name";
        EXPECT_TRUE(sim.readValue(QStringLiteral("wheel"), QStringLiteral("friction")).isValid());
        sim.stop();
    }
}

// A body deleted leaves its shapes on the canvas rather than taking them with
// it, and the property table follows the selection rather than a body that is
// no longer there.
TEST(BodyEditing, DeletingABodyLeavesItsShapes)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setEditorMode(EditorMode::Physics);

    ShapeItem *shape = rectangle(&scene, QStringLiteral("hull"), QPointF(0, 0));
    scene.notifyShapesChanged();
    scene.selectForPhysics(shape);
    PhysicsBody *body = scene.createBodyFromSelection();
    ASSERT_TRUE(body);
    scene.clearPhysicsSelection();

    PhysicsPropertyPane pane;
    scene.selectForPhysics(shape);
    pane.attach(&scene);
    EXPECT_FALSE(pane.rows(EditorMode::Physics).empty()) << "the table has the body's rows";

    scene.destroyBody(body);
    EXPECT_EQ(scene.bodies().size(), 0);
    EXPECT_EQ(scene.shapes().size(), 1) << "the shape outlives the body it was in";
    EXPECT_TRUE(scene.shapes().first()->scene()) << "and is still on the canvas";
}
