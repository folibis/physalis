#pragma once

#include "ShapePropertyPaneBase.h"

class PolygonPropertyPane : public ShapePropertyPaneBase
{
public:
    explicit PolygonPropertyPane(QObject *parent = nullptr) : ShapePropertyPaneBase(parent) {}

protected:
    std::vector<PropertyRow> extraRows(ShapeItem *item) const override;
    std::vector<PropertyRow> extraDefaultRows(ShapeItem *item) const override;
};
