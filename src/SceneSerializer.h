// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QJsonObject>
#include <QString>

class CanvasScene;
class ShapeItem;

namespace SceneSerializer {

// 2: gravity is an acceleration in metres and is handed to the engine as it
// stands. Version 1 scenes were run with it quoted at 50 px per metre instead,
// so they are loaded with the world setting that still does that turned on --
// otherwise every one of them would move at a different speed than it was
// built at.
inline constexpr int kFormatVersion = 2;

QJsonObject save(const CanvasScene *scene);

QJsonObject shapeToJson(const ShapeItem *shape);

ShapeItem *shapeFromJson(const QJsonObject &object);

bool load(CanvasScene *scene, const QJsonObject &document, QString *error);

bool saveToFile(const CanvasScene *scene, const QString &path, QString *error);
bool loadFromFile(CanvasScene *scene, const QString &path, QString *error);

} // namespace SceneSerializer
