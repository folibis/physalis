#pragma once

#include <QIcon>

class QColor;
class ShapeItem;

namespace ObjectIcons {

int size();

QIcon forShape(ShapeItem *shape);
QIcon forBody(const QColor &color);
QIcon forJoint(const QColor &color);

} // namespace ObjectIcons
