#pragma once

// Scenes built in code, so a test never depends on a file it did not create.
// Everything here is deliberately small but complete: shapes, bodies, a joint
// and rules, which is what the interface tests need to have something to show.

#include "CanvasScene.h"
#include "CircleItem.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "ShapeItem.h"

#include <QVariantMap>

namespace Fixtures {

// A cart on a ground plane: five shapes, five bodies, two wheel joints and
// two rules. Enough for the object tree, the rules panel and the property
// table to all have something real to display.
inline void buildCart(CanvasScene *scene)
{
    scene->setSimulationEngineName(QStringLiteral("Box2D"));

    const auto addRect = [scene](const QString &name, const QRectF &rect,
                                 const QPointF &pos) {
        auto *item = new RectangleItem;
        item->setRect(rect);
        item->setPos(pos);
        item->setName(name);
        scene->addItem(item);
        return item;
    };
    const auto addCircle = [scene](const QString &name, qreal radius,
                                   const QPointF &pos) {
        auto *item = new CircleItem;
        item->setRect(QRectF(0, 0, radius * 2.0, radius * 2.0));
        item->setPos(pos);
        item->setName(name);
        scene->addItem(item);
        return item;
    };

    ShapeItem *ground = addRect(QStringLiteral("ground"), QRectF(0, 0, 900, 40),
                                QPointF(-450, 260));
    ShapeItem *wall = addRect(QStringLiteral("wall"), QRectF(0, 0, 40, 200),
                              QPointF(380, 60));
    ShapeItem *chassis = addRect(QStringLiteral("chassis"), QRectF(0, 0, 220, 50),
                                 QPointF(-260, 170));
    ShapeItem *frontWheel = addCircle(QStringLiteral("frontWheel"), 35,
                                      QPointF(-90, 190));
    ShapeItem *rearWheel = addCircle(QStringLiteral("rearWheel"), 35,
                                     QPointF(-250, 190));
    scene->notifyShapesChanged();

    const EditorMode was = scene->editorMode();
    scene->setEditorMode(EditorMode::Physics);

    const auto makeBody = [scene](ShapeItem *shape, physics::BodyType type) {
        scene->clearPhysicsSelection();
        scene->selectForPhysics(shape, true);
        PhysicsBody *body = scene->createBodyFromSelection();
        body->props().type = type;
        // Contact events on everything, so a contact rule has both sides.
        shape->part().enableContactEvents = true;
        scene->clearPhysicsSelection();
        return body;
    };

    makeBody(ground, physics::BodyType::Static);
    PhysicsBody *wallBody = makeBody(wall, physics::BodyType::Static);
    PhysicsBody *chassisBody = makeBody(chassis, physics::BodyType::Dynamic);
    PhysicsBody *frontBody = makeBody(frontWheel, physics::BodyType::Dynamic);
    PhysicsBody *rearBody = makeBody(rearWheel, physics::BodyType::Dynamic);

    // Sprung and driven, so there is something to read back and something for
    // a rule to change. Left at the defaults these are all zero.
    QVariantMap params;
    params.insert(QStringLiteral("enableSpring"), true);
    params.insert(QStringLiteral("hertz"), 3.0);
    params.insert(QStringLiteral("dampingRatio"), 0.7);
    params.insert(QStringLiteral("enableMotor"), true);
    params.insert(QStringLiteral("motorSpeed"), 180.0);
    params.insert(QStringLiteral("maxMotorTorque"), 500.0);
    scene->createJoint(QStringLiteral("wheel"), chassisBody, frontBody, 1, params);
    scene->createJoint(QStringLiteral("wheel"), chassisBody, rearBody, 1, params);

    Rule stop;
    stop.subjectName = chassis->name();
    stop.eventId = QStringLiteral("contactBegin");
    stop.conditionValue = wall->name();
    stop.targetName = chassisBody->name();
    stop.propertyKey = QStringLiteral("velocityX");
    stop.op = Rule::Op::Set;
    stop.value = 0.0;

    Rule slow;
    slow.subjectName = Rule::world();
    slow.conditionKey = QStringLiteral("time");
    slow.compare = Rule::Compare::Greater;
    slow.conditionValue = 5.0;
    slow.targetName = wallBody->name();
    slow.propertyKey = QStringLiteral("isEnabled");
    slow.op = Rule::Op::Set;
    slow.value = false;

    scene->setRules({ stop, slow });
    scene->setEditorMode(was);
}

} // namespace Fixtures
