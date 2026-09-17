#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "PhysicsTypes.h"

class ShapeItem;

// A body the mouse can fling during a run: pull back from it, let go, and it is
// pushed the other way through its centre of mass -- harder the further it was
// pulled. These belong to the body; how the pull line is drawn is an application
// setting. The push itself is the engine's, found by role.
struct ShotSettings {
    bool enabled = false;
    // The impulse at full pull, in the units the engine's impulse properties
    // take -- so the number means what the same number does in a rule.
    qreal fullImpulse = 1.0;
    // Scene units of pull that count as full power. Pulling further adds nothing.
    qreal maxPull = 300.0;
};

class PhysicsBody : public QObject
{
    Q_OBJECT

public:
    explicit PhysicsBody(QObject *parent = nullptr);
    ~PhysicsBody() override;

    QString name() const { return m_props.name; }
    void setName(const QString &name);

    const QVector<ShapeItem *> &shapes() const { return m_shapes; }
    void addShape(ShapeItem *shape);
    void removeShape(ShapeItem *shape);
    // Puts `now` where `previous` sat, rather than dropping one and appending
    // the other. The first shape is the body's reference transform -- see
    // originScenePos() -- so a convert-in-place that appended would move the
    // whole body's origin onto a different shape.
    bool replaceShape(ShapeItem *previous, ShapeItem *now);
    bool isEmpty() const { return m_shapes.isEmpty(); }

    const physics::BodyDesc &props() const { return m_props; }
    physics::BodyDesc &props() { return m_props; }
    void notifyPropertyChanged() { emit propertyChanged(); }

    physics::BodyDesc toBodyDesc() const;

    const ShotSettings &shot() const { return m_shot; }
    ShotSettings &shot() { return m_shot; }

    QPointF originScenePos() const;

    // The body's orientation in degrees, taken from the same reference shape
    // as originScenePos(). Together they are the body's transform.
    qreal rotationDegrees() const;

    QPointF centerOfMassScenePos() const;

    bool isAsleep() const { return m_asleep; }
    void setAsleep(bool asleep);

    // Run state, not document state: a rule took this body out of the world,
    // so nothing of it is drawn until the run ends -- its shapes, its axes and
    // the joints that were attached to it. Never saved; the scene still has
    // the body, and stopping brings all of it back.
    bool isRemoved() const { return m_removed; }
    void setRemoved(bool removed);

    // Made by a rule during a run, and gone when it ends: never saved.
    bool isRunOnly() const { return m_runOnly; }
    void setRunOnly(bool runOnly) { m_runOnly = runOnly; }

signals:
    void propertyChanged();
    // Old name first. See setName().
    void nameChanged(const QString &previous, const QString &current);
    void membershipChanged();

private:
    // parts/position/rotationDegrees are left unset here and filled in by
    // toBodyDesc() from the member shapes; everything else is user-editable.
    physics::BodyDesc m_props;

    QVector<ShapeItem *> m_shapes;
    ShotSettings m_shot;
    bool m_asleep = false;
    bool m_removed = false;
    bool m_runOnly = false;
};
