// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "ClipboardProbe.h"
#include "MainWindow.h"
#include "IPhysicsEngine.h"
#include "Joint.h"
#include "CircleItem.h"
#include "PhysicsBody.h"
#include "PropertyPane/PhysicsPropertyPane.h"
#include "RectangleItem.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"
#include "UndoStack.h"

#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
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

// Copying a mechanism has to carry the mechanism. Two bodies held together by
// a joint come back as two bodies still held together -- to each other, not to
// the pair they were copied from. Copy used to write out the shapes alone, so
// a duplicated wheel-and-axle arrived as loose plates in no body with nothing
// joining them, and the whole assembly had to be built again by hand.
// Ctrl+C then Ctrl+V on a mechanism, through the actions the keys are bound to
// rather than through the serializer. The serializer carried the joint and a
// test of it passed, while nothing happened on the canvas: Copy asked
// activeItem(), which is Edit mode's selection, so a mechanism picked in
// Physics mode left the action greyed out and the keystroke did nothing.
TEST(BodyEditing, CopyAndPasteCarryAMechanismThroughTheActions)
{
    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_NE(scene, nullptr) << "the window has no scene";
    auto *copy = window.findChild<QAction *>(QStringLiteral("actionCopy"));
    auto *paste = window.findChild<QAction *>(QStringLiteral("actionPaste"));
    ASSERT_NE(copy, nullptr) << "the window has no Copy action";
    ASSERT_NE(paste, nullptr) << "the window has no Paste action";

    if (!theClipboardAnswers())
        GTEST_SKIP() << "the system clipboard is held by something else on this machine";
    QGuiApplication::clipboard()->clear();

    scene->setSimulationEngineName(QStringLiteral("Box2D"));
    scene->setEditorMode(EditorMode::Physics);

    ShapeItem *armShape = rectangle(scene, QStringLiteral("arm"), QPointF(0, 0));
    ShapeItem *hubShape = rectangle(scene, QStringLiteral("hub"), QPointF(100, 0));
    scene->notifyShapesChanged();

    scene->selectForPhysics(armShape, true);
    PhysicsBody *arm = scene->createBodyFromSelection();
    ASSERT_TRUE(arm);
    arm->props().type = physics::BodyType::Dynamic;
    scene->clearPhysicsSelection();

    scene->selectForPhysics(hubShape, true);
    PhysicsBody *hub = scene->createBodyFromSelection();
    ASSERT_TRUE(hub);
    hub->props().type = physics::BodyType::Static;
    scene->clearPhysicsSelection();

    auto engine = physics::EngineRegistry::create(QStringLiteral("Box2D"));
    ASSERT_TRUE(engine);
    ASSERT_FALSE(engine->jointTypes().isEmpty());
    const physics::JointType type = engine->jointTypes().first();
    ASSERT_TRUE(scene->createJoint(type.id, arm, hub, type.anchorCount, {}));

    // Both bodies picked, the way picking a mechanism on the canvas leaves it.
    scene->selectForPhysics(armShape, true);
    scene->selectForPhysics(hubShape, true);
    ASSERT_EQ(scene->physicsSelection().size(), 2);

    ASSERT_TRUE(copy->isEnabled())
        << "a mechanism is picked in Physics mode and Copy is greyed out";
    copy->trigger();
    SKIP_IF_THE_CLIPBOARD_STOPPED_ANSWERING();
    ASSERT_FALSE(QGuiApplication::clipboard()->text().isEmpty())
        << "Copy wrote nothing to the clipboard";
    ASSERT_TRUE(paste->isEnabled()) << "the clipboard holds a mechanism and Paste is greyed out";

    paste->trigger();
    EXPECT_EQ(scene->shapes().size(), 4) << "the paste put down no shapes";
    EXPECT_EQ(scene->bodies().size(), 4) << "the pasted shapes landed in no bodies of their own";
    ASSERT_EQ(scene->joints().size(), 2) << "the joint between the two bodies was not pasted";

    // And it holds the copies, not the originals it was copied from.
    Joint *pasted = scene->joints().last();
    EXPECT_NE(pasted->bodyA(), arm);
    EXPECT_NE(pasted->bodyB(), hub);
    EXPECT_NE(pasted->bodyA(), pasted->bodyB());

    QGuiApplication::clipboard()->clear();
}

TEST(BodyEditing, CopyingTwoJointedBodiesCarriesTheJoint)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setEditorMode(EditorMode::Physics);

    ShapeItem *armShape = rectangle(&scene, QStringLiteral("arm"), QPointF(0, 0));
    ShapeItem *hubShape = rectangle(&scene, QStringLiteral("hub"), QPointF(100, 0));
    scene.notifyShapesChanged();

    scene.selectForPhysics(armShape, true);
    PhysicsBody *arm = scene.createBodyFromSelection();
    ASSERT_TRUE(arm);
    arm->setName(QStringLiteral("arm_body"));
    arm->props().type = physics::BodyType::Dynamic;
    scene.clearPhysicsSelection();

    scene.selectForPhysics(hubShape, true);
    PhysicsBody *hub = scene.createBodyFromSelection();
    ASSERT_TRUE(hub);
    hub->setName(QStringLiteral("hub_body"));
    hub->props().type = physics::BodyType::Static;
    scene.clearPhysicsSelection();

    auto engine = physics::EngineRegistry::create(QStringLiteral("Box2D"));
    ASSERT_TRUE(engine);
    ASSERT_FALSE(engine->jointTypes().isEmpty());
    const physics::JointType type = engine->jointTypes().first();
    Joint *joint = scene.createJoint(type.id, arm, hub, type.anchorCount, {});
    ASSERT_TRUE(joint);
    joint->setName(QStringLiteral("elbow"));

    ASSERT_EQ(scene.bodies().size(), 2);
    ASSERT_EQ(scene.joints().size(), 1);

    // Both bodies picked, the way picking a mechanism on the canvas leaves it.
    const QJsonObject fragment =
        SceneSerializer::fragmentToJson(&scene, {armShape, hubShape});
    const QVector<ShapeItem *> copies =
        SceneSerializer::fragmentFromJson(&scene, fragment, QPointF(300, 0));

    ASSERT_EQ(copies.size(), 2) << "the copy did not bring both shapes";
    EXPECT_EQ(scene.shapes().size(), 4);
    ASSERT_EQ(scene.bodies().size(), 4) << "the copies did not land in bodies of their own";
    ASSERT_EQ(scene.joints().size(), 2) << "the joint between them was not copied";

    // Each copy is in a body, and those are new bodies, not the originals.
    PhysicsBody *copiedA = copies.at(0)->body();
    PhysicsBody *copiedB = copies.at(1)->body();
    ASSERT_TRUE(copiedA);
    ASSERT_TRUE(copiedB);
    EXPECT_NE(copiedA, arm);
    EXPECT_NE(copiedB, hub);
    EXPECT_NE(copiedA, copiedB) << "both copied shapes landed in one body";

    // And the new joint holds the two copies to each other.
    Joint *copied = nullptr;
    for (Joint *one : scene.joints()) {
        if (one != joint)
            copied = one;
    }
    ASSERT_TRUE(copied);
    EXPECT_EQ(copied->bodyA(), copiedA) << "the copied joint still holds the original body";
    EXPECT_EQ(copied->bodyB(), copiedB) << "the copied joint still holds the original body";
    EXPECT_NE(copied->name(), joint->name()) << "the copy took the original's name";

    // The body type travels too -- a static hub copied as dynamic would fall.
    EXPECT_EQ(copiedA->props().type, physics::BodyType::Dynamic);
    EXPECT_EQ(copiedB->props().type, physics::BodyType::Static);

    // And the copy stands where it was put, not on top of the original.
    EXPECT_NEAR(copies.at(0)->pos().x(), armShape->pos().x() + 300.0, 1e-9);

    // A joint with only one end in the selection has nothing to hold on this
    // side, so it is left out and said so.
    int leftOut = 0;
    SceneSerializer::fragmentToJson(&scene, {armShape}, &leftOut);
    EXPECT_EQ(leftOut, 1) << "a half-selected joint was carried or went uncounted";
}
