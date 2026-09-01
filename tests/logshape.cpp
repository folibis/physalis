#include "CanvasScene.h"
#include "SceneFixtures.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "ShapeItem.h"
#include "SceneSerializer.h"
#include "PropertyPane/PropertyPane.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <cstdio>


TEST(LogShape, Behaves)
{
    CanvasScene scene;
    QString error;
    Fixtures::buildCart(&scene);

    ShapeItem *chassisShape = nullptr;
    for (ShapeItem *s : scene.shapes())
        if (s->name() == QLatin1String("chassis"))
            chassisShape = s;
    chassisShape->setRotation(23.5);

    EXPECT_TRUE(qFuzzyCompare(
              scene.readSceneValue(QStringLiteral("chassis"),
                                   QStringLiteral("shape.rotation")).toDouble(), 23.5)) << "rotation" << " -- " << (QStringLiteral("%1").arg(scene.readSceneValue(QStringLiteral("chassis"),
                                                        QStringLiteral("shape.rotation")).toDouble())).toStdString();
    EXPECT_TRUE(scene.readSceneValue(QStringLiteral("chassis"),
                                        QStringLiteral("shape.width")).toDouble() > 0) << "width";
    EXPECT_TRUE(scene.readSceneValue(QStringLiteral("chassis"),
                               QStringLiteral("shape.borderWidth")).isValid()) << "border width";

    for (Joint *j : scene.joints()) {
        const QVariant hertz = scene.readSceneValue(j->name(), QStringLiteral("hertz"));
        const QVariant motor = scene.readSceneValue(j->name(), QStringLiteral("motorSpeed"));
        EXPECT_TRUE(hertz.isValid()) << "joint parameter reads back";
        break;
    }

    for (PhysicsBody *b : scene.bodies()) {
        if (b->name() != QLatin1String("body_1"))
            continue;
        EXPECT_TRUE(scene.readSceneValue(b->name(), QStringLiteral("body.angle")).isValid()) << "body angle";
        EXPECT_TRUE(scene.readSceneValue(b->name(), QStringLiteral("body.positionY")).isValid()) << "body position Y";
    }
}
