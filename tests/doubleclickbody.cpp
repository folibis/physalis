#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <gtest/gtest.h>

// In Physics mode a double-click on a loose shape makes it a body: a dynamic one,
// or with Ctrl held a static one.

namespace {

void send(CanvasScene *scene, QEvent::Type type, const QPointF &at, Qt::KeyboardModifiers modifiers)
{
    QGraphicsSceneMouseEvent event(type);
    event.setScenePos(at);
    event.setScreenPos(at.toPoint());
    event.setButton(Qt::LeftButton);
    event.setButtons(type == QEvent::GraphicsSceneMouseRelease ? Qt::NoButton : Qt::LeftButton);
    event.setModifiers(modifiers);
    QCoreApplication::sendEvent(scene, &event);
}

physics::BodyType doubleClickMakes(Qt::KeyboardModifiers modifiers)
{
    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    EXPECT_TRUE(scene);
    if (!scene)
        return physics::BodyType::Kinematic;

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    shape->setPos(100, 100);
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);

    const QPointF at(120, 120);
    send(scene, QEvent::GraphicsSceneMousePress, at, modifiers);
    send(scene, QEvent::GraphicsSceneMouseRelease, at, modifiers);
    send(scene, QEvent::GraphicsSceneMouseDoubleClick, at, modifiers);
    send(scene, QEvent::GraphicsSceneMouseRelease, at, modifiers);

    EXPECT_TRUE(shape->body()) << "the double-click made no body";
    return shape->body() ? shape->body()->props().type : physics::BodyType::Kinematic;
}

} // namespace

TEST(DoubleClickBody, PlainMakesDynamic)
{
    EXPECT_EQ(doubleClickMakes(Qt::NoModifier), physics::BodyType::Dynamic);
}

TEST(DoubleClickBody, CtrlMakesStatic)
{
    EXPECT_EQ(doubleClickMakes(Qt::ControlModifier), physics::BodyType::Static);
}
