#pragma once

#include <QJsonObject>
#include <QString>

class CanvasScene;
class ShapeItem;

namespace SceneSerializer {

inline constexpr int kFormatVersion = 1;

QJsonObject save(const CanvasScene *scene);

QJsonObject shapeToJson(const ShapeItem *shape);

ShapeItem *shapeFromJson(const QJsonObject &object);

bool load(CanvasScene *scene, const QJsonObject &document, QString *error);

bool saveToFile(const CanvasScene *scene, const QString &path, QString *error);
bool loadFromFile(CanvasScene *scene, const QString &path, QString *error);

} // namespace SceneSerializer
