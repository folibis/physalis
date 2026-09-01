#pragma once

#include "PropertyPane.h"
#include "PhysicsTypes.h"

#include <functional>

class CanvasScene;
class ShapeItem;
class PhysicsBody;

class PhysicsPropertyPane : public PropertyPane
{
public:
    explicit PhysicsPropertyPane(QObject *parent = nullptr) : PropertyPane(parent) {}

    std::vector<PropertyRow> rows(EditorMode mode) const override;
    std::vector<PropertyRow> defaultRows(EditorMode mode) const override;
    void attach(QObject *target) override;

private:
    static PropertyRow bodyTypeRow(physics::BodyDesc *props,
                                   const std::function<void()> &changed);
    static std::vector<PropertyRow> bodyPropRows(physics::BodyDesc *props,
                                                 const std::function<void()> &changed);
    // `relayout` is for a setting that changes which rows exist -- the sensor
    // flag takes the contact settings away with it.
    static std::vector<PropertyRow> shapePropRows(physics::ShapePart *part,
                                                  const std::function<void()> &changed,
                                                  const std::function<void()> &relayout = {});

    static std::vector<PropertyRow> bodyIdentityRows(PhysicsBody *body);
    static std::vector<PropertyRow> shapeIdentityRows(ShapeItem *shape);

    CanvasScene *m_scene = nullptr;
};
