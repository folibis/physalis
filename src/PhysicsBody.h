#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "PhysicsTypes.h"

class ShapeItem;

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
    bool isEmpty() const { return m_shapes.isEmpty(); }

    const physics::BodyDesc &props() const { return m_props; }
    physics::BodyDesc &props() { return m_props; }
    void notifyPropertyChanged() { emit propertyChanged(); }

    physics::BodyDesc toBodyDesc() const;

    QPointF originScenePos() const;

    // The body's orientation in degrees, taken from the same reference shape
    // as originScenePos(). Together they are the body's transform.
    qreal rotationDegrees() const;

    QPointF centerOfMassScenePos() const;

    bool isAsleep() const { return m_asleep; }
    void setAsleep(bool asleep);

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
    bool m_asleep = false;
};
