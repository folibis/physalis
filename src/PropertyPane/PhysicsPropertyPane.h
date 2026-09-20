// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include "PropertyPane.h"
#include "PhysicsTypes.h"

#include <functional>

class CanvasScene;
class ShapeItem;
class PhysicsBody;
struct ShotSettings;

class PhysicsPropertyPane : public PropertyPane
{
public:
    explicit PhysicsPropertyPane(QObject *parent = nullptr) : PropertyPane(parent) {}

    std::vector<PropertyRow> rows(EditorMode mode) const override;
    std::vector<PropertyRow> defaultRows(EditorMode mode) const override;
    void attach(QObject *target) override;

private:
    // The editor's own rows. Everything else a body or a shape has comes from
    // the engine's catalogue, and this pane names none of it.
    static PropertyRow bodyTypeRow(physics::BodyDesc *props,
                                   const std::function<void()> &changed);
    static PropertyRow enabledRow(physics::BodyDesc *props,
                                  const std::function<void()> &changed);
    static std::vector<PropertyRow> bodyIdentityRows(PhysicsBody *body);
    static std::vector<PropertyRow> shotRows(ShotSettings *shot, const std::function<void()> &changed);
    static std::vector<PropertyRow> shapeIdentityRows(ShapeItem *shape);

    CanvasScene *m_scene = nullptr;
};
