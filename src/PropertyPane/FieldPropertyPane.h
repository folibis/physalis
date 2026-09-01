#pragma once

#include "PropertyPane.h"
#include "PhysicsTypes.h"

#include <functional>

class CanvasScene;

class FieldPropertyPane : public PropertyPane
{
public:
    explicit FieldPropertyPane(QObject *parent = nullptr) : PropertyPane(parent) {}

    std::vector<PropertyRow> rows(EditorMode mode) const override;
    std::vector<PropertyRow> defaultRows(EditorMode mode) const override;
    void attach(QObject *target) override;

private:
    static std::vector<PropertyRow> worldRows(physics::WorldDesc *world,
                                              const std::function<void()> &changed);

    CanvasScene *m_scene = nullptr;
};
