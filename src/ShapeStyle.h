// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

// How a shape is drawn when it is created. One of these per kind, held in
// Options; a shape keeps its own copy from then on.
struct ShapeStyle {
    QColor body { 0x00, 0xaa, 0xff, 0x80 };
    QColor border { 0x00, 0x00, 0xff, 0xcc };
    qreal borderWidth = 2.0;
    Qt::PenStyle borderStyle = Qt::SolidLine;

    // Edit mode knows three kinds. A polyline is a polygon here: whether it
    // ends up a chain or loose segments is decided by the physics it is given.
    static QStringList kinds()
    {
        return { QStringLiteral("rectangle"), QStringLiteral("circle"),
                 QStringLiteral("polygon") };
    }

    // What a shape calls itself, mapped onto one of the three above.
    static QString kindOf(const QString &typeName)
    {
        return typeName == QLatin1String("polyline") ? QStringLiteral("polygon") : typeName;
    }

    static QString kindLabel(const QString &kind)
    {
        if (kind == QLatin1String("rectangle")) return QObject::tr("Rectangle");
        if (kind == QLatin1String("circle"))    return QObject::tr("Circle");
        return QObject::tr("Polygon");
    }

    static ShapeStyle defaultFor(const QString &kind)
    {
        ShapeStyle style;
        if (kind == QLatin1String("circle"))
            style.body = QColor(0x55, 0xff, 0x00, 0x80);
        else if (kind == QLatin1String("polygon"))
            style.body = QColor(0xff, 0x55, 0xff, 0x80);
        return style;
    }

    static QVector<Qt::PenStyle> penStyles()
    {
        return { Qt::SolidLine, Qt::DashLine, Qt::DotLine,
                 Qt::DashDotLine, Qt::DashDotDotLine };
    }

    static QString penStyleLabel(Qt::PenStyle style)
    {
        switch (style) {
        case Qt::DashLine:       return QObject::tr("Dashed");
        case Qt::DotLine:        return QObject::tr("Dotted");
        case Qt::DashDotLine:    return QObject::tr("Dash-dot");
        case Qt::DashDotDotLine: return QObject::tr("Dash-dot-dot");
        default:                 return QObject::tr("Solid");
        }
    }

    static QString penStyleName(Qt::PenStyle style)
    {
        switch (style) {
        case Qt::DashLine:       return QStringLiteral("dash");
        case Qt::DotLine:        return QStringLiteral("dot");
        case Qt::DashDotLine:    return QStringLiteral("dashdot");
        case Qt::DashDotDotLine: return QStringLiteral("dashdotdot");
        default:                 return QStringLiteral("solid");
        }
    }

    static Qt::PenStyle penStyleFromName(const QString &name)
    {
        if (name == QLatin1String("dash"))       return Qt::DashLine;
        if (name == QLatin1String("dot"))        return Qt::DotLine;
        if (name == QLatin1String("dashdot"))    return Qt::DashDotLine;
        if (name == QLatin1String("dashdotdot")) return Qt::DashDotDotLine;
        return Qt::SolidLine;
    }
};
