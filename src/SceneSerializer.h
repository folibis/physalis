// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QJsonObject>
#include <QString>

class CanvasScene;
class ShapeItem;
class RayItem;
class ExplosionItem;

namespace SceneSerializer {

inline constexpr int kFormatVersion = 1;

QJsonObject save(const CanvasScene *scene);

QJsonObject shapeToJson(const ShapeItem *shape);

ShapeItem *shapeFromJson(const QJsonObject &object);

// A ray and a blast go through the same door as a shape does, so the clipboard
// and the file cannot drift apart in what they carry. Both are made by the
// scene rather than standing on their own, so reading one needs it.
QJsonObject rayToJson(const RayItem *ray);
RayItem *rayFromJson(CanvasScene *scene, const QJsonObject &object);
QJsonObject explosionToJson(const ExplosionItem *explosion);
ExplosionItem *explosionFromJson(CanvasScene *scene, const QJsonObject &object);

bool load(CanvasScene *scene, const QJsonObject &document, QString *error);

bool saveToFile(const CanvasScene *scene, const QString &path, QString *error);
bool loadFromFile(CanvasScene *scene, const QString &path, QString *error);

} // namespace SceneSerializer
