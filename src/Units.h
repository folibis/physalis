// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QString>
#include <QVector>

// How a length on the canvas is written for a person to read. The scene itself
// measures in its own units -- what a drawing is drawn in -- and `WorldDesc`'s
// pixelsPerMeter is the only thing that turns those into a real distance. That
// conversion is the application's (see "pixelsPerMeter converts, and does
// nothing else"), so this is where it is written down once: the rulers use it
// now, and anything else that has to quote a distance can.
namespace Units {

enum class Unit { Scene, Millimetres, Centimetres, Metres, Kilometres };

struct Entry {
    Unit unit;
    // What the INI carries. Stable, so renaming a label cannot lose a choice.
    QString key;
    // For a menu or a combo box.
    QString label;
    // Written after a number, where there is room for one.
    QString suffix;
};

QVector<Entry> all();

QString key(Unit unit);
// An unknown key is scene units rather than an error: a settings file written
// by a later build naming a unit this one has never heard of still opens.
Unit fromKey(const QString &key, Unit fallback = Unit::Scene);
QString label(Unit unit);
QString suffix(Unit unit);

// How many scene units make one of these. Scene units are one to one; the
// rest go through the scale, so a scene at 1000 px per metre reads a tenth of
// what the same drawing reads at 100.
qreal sceneUnitsPer(Unit unit, qreal pixelsPerMeter);

// A scene coordinate as a number in this unit.
qreal fromScene(qreal sceneValue, Unit unit, qreal pixelsPerMeter);

// That number written out, with as many decimals as `step` needs and no
// trailing zeros beyond them -- so a ruler stepping by 0.5 reads "1.5" and one
// stepping by 10 reads "20", never "20.000".
QString format(qreal sceneValue, Unit unit, qreal pixelsPerMeter, qreal step);

// The next 1, 2 or 5 times a power of ten at or above `rough`. What a ruler
// picks its tick spacing with: a step of "every 23.4 cm" is a step nobody can
// read a position off.
qreal niceStep(qreal rough);

} // namespace Units
