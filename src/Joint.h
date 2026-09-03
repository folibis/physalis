#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include "JointTypes.h"

class PhysicsBody;

class Joint : public QObject
{
    Q_OBJECT

public:
    explicit Joint(QObject *parent = nullptr);

    QString name() const { return m_name; }
    void setName(const QString &name);

    QString typeId() const { return m_typeId; }
    void setTypeId(const QString &typeId);

    PhysicsBody *bodyA() const { return m_bodyA; }
    PhysicsBody *bodyB() const { return m_bodyB; }
    void setBodies(PhysicsBody *bodyA, PhysicsBody *bodyB);

    enum class End { A, B };

    int anchorCount() const { return m_anchorCount; }
    void setAnchorCount(int count) { m_anchorCount = qBound(0, count, 2); }

    QPointF anchorScenePos(End end) const;
    void setAnchorScenePos(End end, const QPointF &scenePos);

    QPointF axisScene() const;
    void setAxisScene(const QPointF &direction);

    const QVariantMap &params() const { return m_params; }
    QVariantMap &params() { return m_params; }
    void notifyPropertyChanged() { emit propertyChanged(); }

    bool collideConnected() const { return m_collideConnected; }
    void setCollideConnected(bool collide);

    // Run state, not document state: a rule took this joint out of the world,
    // so it stops being drawn until the run ends. Never saved -- the joint is
    // still every bit as much part of the scene.
    bool isBroken() const { return m_broken; }
    void setBroken(bool broken);

    physics::JointDesc toJointDesc(int bodyHandleA, int bodyHandleB) const;

signals:
    void propertyChanged();
    // Old name first. See setName().
    void nameChanged(const QString &previous, const QString &current);

private:
    static QPointF toBodyFrame(PhysicsBody *body, const QPointF &scenePos);
    static QPointF toSceneFrame(PhysicsBody *body, const QPointF &localPos);

    QString m_name;
    QString m_typeId;

    QPointer<PhysicsBody> m_bodyA;
    QPointer<PhysicsBody> m_bodyB;

    QPointF m_localAnchorA;
    QPointF m_localAnchorB;
    int m_anchorCount = 2;
    QPointF m_localAxis { 1.0, 0.0 };

    QVariantMap m_params;
    bool m_collideConnected = false;
    bool m_broken = false;
};
