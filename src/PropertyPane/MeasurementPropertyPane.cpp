#include "MeasurementPropertyPane.h"

#include "CanvasScene.h"
#include "Units.h"

#include <QLineF>
#include <QtMath>
#include <cmath>

void MeasurementPropertyPane::attach(QObject *target)
{
    auto *scene = qobject_cast<CanvasScene *>(target);
    if (scene == m_scene)
        return;
    if (m_scene)
        disconnect(m_scene, nullptr, this, nullptr);
    m_scene = scene;
    if (!m_scene)
        return;
    // Every row reads back, the one being typed into included: a point moved
    // changes the widths and the angle beside it. Queued, as the other panes'
    // refresh after an edit is, so it lands after the editor's own signal.
    connect(m_scene, &CanvasScene::measurementChanged, this, &PropertyPane::valueChanged,
            Qt::QueuedConnection);
    // The width row is labelled with its unit.
    connect(m_scene, &CanvasScene::measureUnitChanged, this, &PropertyPane::rowsChanged);
}

void MeasurementPropertyPane::write(const QVector<QPointF> &points) const
{
    if (m_scene)
        m_scene->setMeasurePoints(points);
}

std::vector<PropertyRow> MeasurementPropertyPane::rows(EditorMode mode) const
{
    Q_UNUSED(mode);

    std::vector<PropertyRow> result;
    if (!m_scene || !m_scene->isMeasurementLaid())
        return result;

    const bool protractor = m_scene->measureTool() == CanvasScene::Measure::Angle;
    const QString section = QObject::tr("Measurement");
    const QPointer<CanvasScene> scene = m_scene;
    const auto points = [scene] {
        return scene ? scene->measurePoints() : QVector<QPointF>();
    };

    const auto numeric = [&](const QString &label, std::function<QVariant()> getter,
                             std::function<void(const QVariant &)> setter) {
        PropertyRow row;
        row.label = label;
        row.type = PropertyFieldType::Numeric;
        row.getter = std::move(getter);
        row.setter = std::move(setter);
        row.minValue = -1e6;
        row.maxValue = 1e6;
        row.decimals = 1;
        row.step = 1.0;
        row.section = section;
        row.editsScene = false;
        return row;
    };

    // In the rulers' unit, since that is what the canvas writes beside it.
    // Changing one keeps its end on the line it was on, so the angle stays.
    // A protractor has two arms and a width for each.
    const Units::Unit unit = m_scene->measureUnit();
    const qreal perUnit = Units::sceneUnitsPer(unit, m_scene->pixelsPerMeter());
    const QString suffix = Units::suffix(unit);
    const auto width = [&](int index) {
        QString label = QObject::tr("Width");
        if (protractor)
            label += index == 1 ? QObject::tr(" AB") : QObject::tr(" AC");
        if (!suffix.isEmpty())
            label += QStringLiteral(" (%1)").arg(suffix);
        PropertyRow row = numeric(
            label,
            [points, perUnit, index] {
                const QVector<QPointF> at = points();
                return index >= at.size() ? QVariant()
                                          : QVariant(QLineF(at.at(0), at.at(index)).length() / perUnit);
            },
            [this, points, perUnit, index](const QVariant &v) {
                QVector<QPointF> at = points();
                if (index >= at.size())
                    return;
                QLineF arm(at.at(0), at.at(index));
                // An arm with no length has no direction to keep; it is laid
                // along 0 degrees.
                if (arm.isNull())
                    arm.setP2(arm.p1() + QPointF(1.0, 0.0));
                arm.setLength(qMax(0.0, v.toDouble()) * perUnit);
                at[index] = arm.p2();
                write(at);
            });
        row.minValue = 0.0;
        // As fine as one scene unit is in this unit, and never coarser than a tenth.
        row.decimals = qBound(1, int(std::ceil(std::log10(perUnit))) + 1, 6);
        row.step = 1.0 / perUnit;
        row.tooltip = index == 1
                          ? QObject::tr("How far B is from A. Setting it moves B along the line, so"
                                        " the angle stays as it is.")
                          : QObject::tr("How far C is from A. Setting it moves C along the line, so"
                                        " the angle stays as it is.");
        result.push_back(std::move(row));
    };
    width(1);
    if (protractor)
        width(2);

    const auto coordinate = [&](const QString &label, int index, bool horizontal) {
        result.push_back(numeric(
            label,
            [points, index, horizontal] {
                const QVector<QPointF> at = points();
                if (index >= at.size())
                    return QVariant();
                return QVariant(horizontal ? at.at(index).x() : at.at(index).y());
            },
            [this, points, index, horizontal](const QVariant &v) {
                QVector<QPointF> at = points();
                if (index >= at.size())
                    return;
                if (horizontal)
                    at[index].setX(v.toDouble());
                else
                    at[index].setY(v.toDouble());
                write(at);
            }));
    };
    coordinate(QObject::tr("A X"), 0, true);
    coordinate(QObject::tr("A Y"), 0, false);
    coordinate(QObject::tr("B X"), 1, true);
    coordinate(QObject::tr("B Y"), 1, false);
    if (protractor) {
        coordinate(QObject::tr("C X"), 2, true);
        coordinate(QObject::tr("C Y"), 2, false);
    }

    // Turned about A, with the arm keeping its length. A ruler reads the way
    // the Ctrl snap turns it: 0 degrees to the right, counter-clockwise on
    // screen. A protractor reads the angle between its arms, as the canvas
    // writes it, and setting it swings C on whichever side of AB it is.
    PropertyRow angle = numeric(
        QObject::tr("Angle (%1)").arg(QChar(0x00B0)),
        [points, protractor] {
            const QVector<QPointF> at = points();
            if (!protractor)
                return at.size() < 2 ? QVariant() : QVariant(QLineF(at.at(0), at.at(1)).angle());
            if (at.size() < 3)
                return QVariant();
            const QPointF a = at.at(1) - at.at(0);
            const QPointF b = at.at(2) - at.at(0);
            if (a.isNull() || b.isNull())
                return QVariant();
            return QVariant(qRadiansToDegrees(std::atan2(std::abs(a.x() * b.y() - a.y() * b.x()),
                                                         a.x() * b.x() + a.y() * b.y())));
        },
        [this, points, protractor](const QVariant &v) {
            QVector<QPointF> at = points();
            const int moving = protractor ? 2 : 1;
            if (moving >= at.size())
                return;
            QLineF arm(at.at(0), at.at(moving));
            if (arm.isNull())
                return;
            qreal turned = v.toDouble();
            if (protractor) {
                const qreal base = QLineF(at.at(0), at.at(1)).angle();
                const qreal side = std::fmod(arm.angle() - base + 360.0, 360.0) <= 180.0 ? 1.0 : -1.0;
                turned = base + side * turned;
            }
            arm.setAngle(turned);
            at[moving] = arm.p2();
            write(at);
        });
    angle.minValue = 0.0;
    angle.maxValue = protractor ? 180.0 : 360.0;
    angle.tooltip = protractor
                        ? QObject::tr("The angle at A between AB and AC. Setting it turns C about A;"
                                      " the arm keeps its length.")
                        : QObject::tr("Which way B lies from A: 0 is to the right, 90 straight up."
                                      " Setting it turns B about A; the width stays.");
    result.push_back(std::move(angle));

    return result;
}
