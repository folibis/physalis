#pragma once

#include "PropertyPane.h"

class CanvasScene;

// A point has no geometry and no physics -- just a name and where it is.
class ExplosionPropertyPane : public PropertyPane
{
    Q_OBJECT

public:
    explicit ExplosionPropertyPane(QObject *parent = nullptr) : PropertyPane(parent) {}

    std::vector<PropertyRow> rows(EditorMode mode) const override;
    std::vector<PropertyRow> defaultRows(EditorMode mode) const override;
    void attach(QObject *target) override;

private:
    CanvasScene *m_scene = nullptr;
};
