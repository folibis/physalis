// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "Units.h"

#include <QObject>

#include <cmath>

namespace Units {

QVector<Entry> all()
{
    return {
        { Unit::Scene, QStringLiteral("scene"), QObject::tr("Scene units"), QObject::tr("px") },
        { Unit::Millimetres, QStringLiteral("mm"), QObject::tr("Millimetres"), QObject::tr("mm") },
        { Unit::Centimetres, QStringLiteral("cm"), QObject::tr("Centimetres"), QObject::tr("cm") },
        { Unit::Metres, QStringLiteral("m"), QObject::tr("Metres"), QObject::tr("m") },
        { Unit::Kilometres, QStringLiteral("km"), QObject::tr("Kilometres"), QObject::tr("km") },
    };
}

QString key(Unit unit)
{
    for (const Entry &entry : all()) {
        if (entry.unit == unit)
            return entry.key;
    }
    return QStringLiteral("scene");
}

Unit fromKey(const QString &key, Unit fallback)
{
    for (const Entry &entry : all()) {
        if (entry.key == key)
            return entry.unit;
    }
    return fallback;
}

QString label(Unit unit)
{
    for (const Entry &entry : all()) {
        if (entry.unit == unit)
            return entry.label;
    }
    return all().first().label;
}

QString suffix(Unit unit)
{
    for (const Entry &entry : all()) {
        if (entry.unit == unit)
            return entry.suffix;
    }
    return all().first().suffix;
}

qreal sceneUnitsPer(Unit unit, qreal pixelsPerMeter)
{
    const qreal perMetre = pixelsPerMeter > 0.0 ? pixelsPerMeter : 1.0;
    switch (unit) {
    case Unit::Scene:
        return 1.0;
    case Unit::Millimetres:
        return perMetre / 1000.0;
    case Unit::Centimetres:
        return perMetre / 100.0;
    case Unit::Metres:
        return perMetre;
    case Unit::Kilometres:
        return perMetre * 1000.0;
    }
    return 1.0;
}

qreal fromScene(qreal sceneValue, Unit unit, qreal pixelsPerMeter)
{
    return sceneValue / sceneUnitsPer(unit, pixelsPerMeter);
}

QString format(qreal sceneValue, Unit unit, qreal pixelsPerMeter, qreal step)
{
    const qreal value = fromScene(sceneValue, unit, pixelsPerMeter);
    // The step says what the number is worth showing to: quoting more digits
    // than the ticks are apart claims a precision the ruler does not have.
    // Counted as the places the step itself needs rather than from its
    // logarithm, which is right for 0.5 and 0.1 and a digit short for 0.25.
    int decimals = 0;
    if (step > 0.0 && step < 1.0) {
        while (decimals < 6
               && std::fabs(step - std::round(step * std::pow(10.0, decimals))
                                       / std::pow(10.0, decimals))
                      > step * 1e-6) {
            ++decimals;
        }
    }

    QString text = QString::number(value, 'f', decimals);
    // -0 reads as a mistake where the ruler crosses the origin.
    if (text.startsWith(QLatin1Char('-')) && text.toDouble() == 0.0)
        text.remove(0, 1);
    return text;
}

qreal niceStep(qreal rough)
{
    if (!(rough > 0.0) || !std::isfinite(rough))
        return 1.0;
    const qreal decade = std::pow(10.0, std::floor(std::log10(rough)));
    const qreal left = rough / decade;
    if (left <= 1.0)
        return decade;
    if (left <= 2.0)
        return 2.0 * decade;
    if (left <= 5.0)
        return 5.0 * decade;
    return 10.0 * decade;
}

} // namespace Units
