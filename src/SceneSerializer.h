// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVector>

class CanvasScene;
class ShapeItem;
class RayItem;
class ExplosionItem;
class PhysicsBody;

namespace SceneSerializer {

inline constexpr int kFormatVersion = 1;

QJsonObject save(const CanvasScene *scene);

QJsonObject shapeToJson(const ShapeItem *shape);

ShapeItem *shapeFromJson(const QJsonObject &object);

// A piece of a scene rather than the whole of one: the shapes given, the
// bodies they belong to, and the joints between those bodies. What Copy and
// Duplicate carry, so a mechanism taken out of a scene comes back as a
// mechanism rather than as loose shapes.
//
// `jointsLeftOut`, where it is given, counts the joints with only one end in
// the selection -- those are dropped, since there is nothing on this side for
// the other end to hold.
QJsonObject fragmentToJson(const CanvasScene *scene, const QVector<ShapeItem *> &shapes,
                           int *jointsLeftOut = nullptr);

// Reads one back into the scene, moved by `offset`, with fresh names
// throughout. Returns the shapes it made, which is what the caller selects.
QVector<ShapeItem *> fragmentFromJson(CanvasScene *scene, const QJsonObject &fragment,
                                      const QPointF &offset);

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
