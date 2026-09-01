#pragma once

// GoogleTest prints values with operator<<, and knows nothing about Qt's
// types. Without these a failure message either will not compile or comes out
// as a byte dump.

#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QString>

#include <ostream>

inline std::ostream &operator<<(std::ostream &os, const QString &value)
{
    return os << value.toStdString();
}

inline std::ostream &operator<<(std::ostream &os, const QPoint &value)
{
    return os << '(' << value.x() << ", " << value.y() << ')';
}

inline std::ostream &operator<<(std::ostream &os, const QPointF &value)
{
    return os << '(' << value.x() << ", " << value.y() << ')';
}

inline std::ostream &operator<<(std::ostream &os, const QSize &value)
{
    return os << value.width() << 'x' << value.height();
}
